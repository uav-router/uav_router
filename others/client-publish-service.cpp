// clang++ others/client-publish-service.cpp  others/avahi-common.cpp -g -o bintest/avahi-pub $(pkg-config --libs --cflags avahi-client)
// clang others/client-publish-service.cpp -o bintest/avahi-pub $(pkg-config --libs --cflags avahi-client )

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
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-common/alternative.h>
#include <avahi-common/simple-watch.h>
#include <avahi-common/malloc.h>
#include <avahi-common/error.h>
#include <avahi-common/timeval.h>
#include "avahi-common.h"

struct UserData {
    CAvahiEntryGroup *group = NULL;
    CAvahiSimplePoll *simple_poll = NULL;
    CAvahiClient *client = NULL;
    char *name = NULL;
};

static void create_services(UserData* userdata);

bool handle_error(int ret, UserData* userdata, const char* oper) {
    if (ret == AVAHI_ERR_COLLISION) {
        char* n = avahi_alternative_service_name(userdata->name);
        avahi_free(userdata->name);
        userdata->name = n;
        fprintf(stderr, "Service name collision, renaming service to '%s'\n", userdata->name);
        userdata->group->reset();
        create_services(userdata);
        return true;
    }
    if (ret<0) {
        fprintf(stderr, "Failed to %s: %s\n", oper, avahi_strerror(ret));
        userdata->simple_poll->quit();
        return true;
    }
    return false;
}

static void create_services(UserData* userdata) {
    char *n, r[128];
    int ret;
    assert(userdata->client->get());
    /* If this is the first time we're called, let's create a new
     * entry group if necessary */
    if (!userdata->group->is_init()) {
        userdata->group->on_established([userdata](CAvahiEntryGroup*){
            fprintf(stderr, "Service '%s' successfully established.\n", userdata->name);
        });
        userdata->group->on_collision([userdata](CAvahiEntryGroup*){
            char *n;
            /* A service name collision with a remote service
             * happened. Let's pick a new name */
            n = avahi_alternative_service_name(userdata->name);
            avahi_free(userdata->name);
            userdata->name = n;
            fprintf(stderr, "Service name collision, renaming service to '%s'\n", userdata->name);
            /* And recreate the services */
            create_services(userdata);
        });
        userdata->group->on_failure([userdata](CAvahiEntryGroup* g){
            fprintf(stderr, "Entry group failure: %s\n", g->error());
            /* Some kind of failure happened while we were registering our services */
            userdata->simple_poll->quit();
        });
        userdata->group->init(userdata->client->get());
        if (!userdata->group->is_init()) {
            fprintf(stderr, "avahi_entry_group_new() failed: %s\n", userdata->client->error());
            userdata->simple_poll->quit();
            return;
        }
    }    
    if (userdata->group->is_empty()) {
        fprintf(stderr, "Adding service '%s'\n", userdata->name);
        snprintf(r, sizeof(r), "random=%i", rand());
        CAvahiService item(userdata->name,"_ipp._tcp");
        if (handle_error(userdata->group->add_service(item,651,{"test=blah", r}),userdata,"add _ipp._tcp service")) return;
        /* Add the same service for BSD LPR */
        item.type = "_printer._tcp";
        if (handle_error(userdata->group->add_service(item,515),userdata,"add _printer._tcp service")) return;
        if (handle_error(
                userdata->group->add_service_subtype(item,"_magic._sub._printer._tcp"),
                userdata,
                "add _magic._sub._printer._tcp service"
            )) return;
        /* Tell the server to register the service */
        if ((ret = userdata->group->commit()) < 0) {
            fprintf(stderr, "Failed to commit entry group: %s\n", avahi_strerror(ret));
            userdata->simple_poll->quit();
        }
    }
}

int main(AVAHI_GCC_UNUSED int argc, AVAHI_GCC_UNUSED char*argv[]) {
    UserData userdata;
    int error;
    int ret = 1;
    struct timeval tv;
    CAvahiSimplePoll poll;
    CAvahiClient client;
    CAvahiEntryGroup group;
    printf("argc=%i\n",argc);
    const char* service_name = "MegaPrinter";
    if (argc==2) {
        service_name = argv[1];
    }
    /* Allocate main loop object */
    do {
        if (!poll.get()) {
            fprintf(stderr, "Failed to create simple poll object.\n");
            break;
        }
        userdata.simple_poll = &poll;
        userdata.name = avahi_strdup(service_name);
        userdata.client = &client;
        userdata.group = &group;
        /* Allocate a new client */
        client.on_failure([&poll](CAvahiClient* client) {
            fprintf(stderr, "Server connection failure: %s\n", client->error());
            poll.quit();
        });
        client.on_state_changed([&userdata](CAvahiClient* client, AvahiClientState state){
            if (state==AVAHI_CLIENT_S_RUNNING) {
                create_services(&userdata);
            } else if (state==AVAHI_CLIENT_S_COLLISION || state==AVAHI_CLIENT_S_REGISTERING) {
                userdata.group->reset();
            }
        });
        ret = client.init(poll.get());
        if (ret) {
            fprintf(stderr, "Failed to create client: %s\n", avahi_strerror(ret));
            break;
        }
        /* After 10s do some weird modification to the service */
        /*poll.on_timeout(avahi_elapse_time(&tv, 1000*10, 0),[&userdata](CAvahiTimeout* t){
            fprintf(stderr, "Doing some weird modification\n");
            avahi_free(userdata.name);
            userdata.name = avahi_strdup("Modified MegaPrinter");
            if (userdata.client->state() == AVAHI_CLIENT_S_RUNNING) {
                userdata.group->reset();
                create_services(&userdata);
            }
            t->free();
        });*/
        poll.loop();
        ret = 0;
    } while(false);

    /* Cleanup things */
    avahi_free(userdata.name);
    return ret;
}