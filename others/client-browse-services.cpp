// clang++ others/client-browse-services.cpp others/avahi-common.cpp -o bintest/avahi-browse -g $(pkg-config --libs --cflags avahi-client)
/***
  This file is part of avahi.
  avahi is free software; you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation; either version 2.1 of the
  License, or (at your option) any later version.
  avahi is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
  or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General
  Public License for more details.
  You should have received a copy of the GNU Lesser General Public
  License along with avahi; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
***/
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <time.h>
#include <avahi-client/client.h>
#include <avahi-client/lookup.h>
#include <avahi-common/simple-watch.h>
#include <avahi-common/malloc.h>
#include <avahi-common/error.h>
#include <net/if.h>

#include "avahi-common.h"

int main(AVAHI_GCC_UNUSED int argc, AVAHI_GCC_UNUSED char*argv[]) {
    int error;
    int ret = 1;
    /* Allocate main loop object */
    CAvahiSimplePoll poll;
    CAvahiClient client;
    CAvahiServiceBrowser sb;
    CAvahiServiceResolver sr;
    do {
        if (!poll.get()) {
            fprintf(stderr, "Failed to create simple poll object.\n");
            break;
        }
        /* Allocate a new client */
        client.on_failure([&poll](CAvahiClient* client) {
            fprintf(stderr, "Server connection failure: %s\n", client->error());
            poll.quit();
        });
        ret = client.init(poll.get());
        if (ret) {
            fprintf(stderr, "Failed to create client: %s\n", avahi_strerror(ret));
            break;
        }
        sb.on_failure([&poll](CAvahiServiceBrowser *b){
            fprintf(stderr, "(Browser) %s\n", b->error());
            poll.quit();
        });
        sb.on_all_for_now([](CAvahiServiceBrowser*){ fprintf(stderr, "(Browser) ALL_FOR_NOW\n");
        });
        sb.on_cache_exhausted([](CAvahiServiceBrowser*){ fprintf(stderr, "(Browser) CACHE_EXHAUSTED\n");
        });
        sb.on_remove([](CAvahiServiceBrowser *b, CAvahiService item, AvahiLookupResultFlags flags){
            fprintf(stderr, "(Browser) REMOVE: service '%s' of type '%s' in domain '%s'\n", item.name, item.type, item.domain);
        });
        sr.on_failure([](CAvahiServiceResolver* sr, CAvahiService item,
                            const char *host_name, const AvahiAddress *address, uint16_t port,
                            AvahiStringList *txt, AvahiLookupResultFlags flags){
            fprintf(stderr, "(Resolver) Failed to resolve service '%s' of type '%s' in domain '%s': %s\n", item.name, item.type, item.domain, sr->error());
        });
        sr.on_resolve([](CAvahiServiceResolver* sr, CAvahiService item,
                            const char *host_name, const AvahiAddress *address, uint16_t port,
                            AvahiStringList *txt, AvahiLookupResultFlags flags){
            char a[AVAHI_ADDRESS_STR_MAX], *t, ifname[IF_NAMESIZE];
            fprintf(stderr, "Service '%s' of type '%s' in domain '%s', interface %s:\n", item.name, item.type, item.domain, if_indextoname(item.interface,ifname));
            avahi_address_snprint(a, sizeof(a), address);
            t = avahi_string_list_to_string(txt);
            fprintf(stderr,
                    "\t%s:%u (%s)\n"
                    "\tTXT=%s\n"
                    "\tcookie is %u\n"
                    "\tis_local: %i\n"
                    "\tour_own: %i\n"
                    "\twide_area: %i\n"
                    "\tmulticast: %i\n"
                    "\tcached: %i\n",
                    host_name, port, a,
                    t,
                    avahi_string_list_get_service_cookie(txt),
                    !!(flags & AVAHI_LOOKUP_RESULT_LOCAL),
                    !!(flags & AVAHI_LOOKUP_RESULT_OUR_OWN),
                    !!(flags & AVAHI_LOOKUP_RESULT_WIDE_AREA),
                    !!(flags & AVAHI_LOOKUP_RESULT_MULTICAST),
                    !!(flags & AVAHI_LOOKUP_RESULT_CACHED));
            avahi_free(t);
        });
        sb.on_new([&sr](CAvahiServiceBrowser *b, CAvahiService item, AvahiLookupResultFlags flags){
            fprintf(stderr, "(Browser) NEW: service '%s' of type '%s' in domain '%s'\n", item.name, item.type, item.domain);
            bool ret = sr.init(b->client(), item);
            if (!ret)
                fprintf(stderr, "Failed to resolve service '%s': %s\n", item.name, b->error());
        });
        CAvahiService bi;
        bi.interface = if_nametoindex("tap0");
        bi.protocol = AVAHI_PROTO_INET;
        sb.init("_ipp._tcp",client.get(),bi);
        if (sb.get()==nullptr) {
            fprintf(stderr, "Failed to create service browser: %s\n", client.error());
            break;
        }
        /* Run the main loop */
        poll.loop();
        ret = 0;
    } while(false);
    return ret;
}