//g++ geta.cpp -o geta -lanl

#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <string>
#include <functional>
#include <system_error>
#include <vector>
#include <iostream>

static struct gaicb **reqs = NULL;
static int nreqs = 0;

static char *
getcmd(void)
{
   static char buf[256];

   fputs("> ", stdout); fflush(stdout);
   if (fgets(buf, sizeof(buf), stdin) == NULL)
       return NULL;

   if (buf[strlen(buf) - 1] == '\n')
       buf[strlen(buf) - 1] = 0;

   return buf;
}

void notify (sigval v) {
    printf("Complete request!");
}

/* Add requests for specified hostnames */
static void
add_requests(void)
{
   int nreqs_base = nreqs;
   char *host;
   int ret;

   while ((host = strtok(NULL, " "))) {
       nreqs++;
       reqs = (gaicb**)realloc(reqs, sizeof(reqs[0]) * nreqs);

       reqs[nreqs - 1] = (gaicb*)calloc(1, sizeof(*reqs[0]));
       reqs[nreqs - 1]->ar_name = strdup(host);
   }

   sigevent * se = (sigevent *)calloc(1, sizeof(sigevent));
   se->sigev_notify = SIGEV_THREAD;
   se->sigev_notify_function = notify;


   /* Queue nreqs_base..nreqs requests. */

   ret = getaddrinfo_a(GAI_NOWAIT, &reqs[nreqs_base],
                       nreqs - nreqs_base, se);
   if (ret) {
       fprintf(stderr, "getaddrinfo_a() failed: %s\n",
               gai_strerror(ret));
       exit(EXIT_FAILURE);
   }
   usleep(1000000);
}

/* Wait until at least one of specified requests completes */
static void
wait_requests(void)
{
   char *id;
   int ret, n;
   struct gaicb const **wait_reqs = (gaicb const **)calloc(nreqs, sizeof(*wait_reqs));
               /* NULL elements are ignored by gai_suspend(). */

   while ((id = strtok(NULL, " ")) != NULL) {
       n = atoi(id);

       if (n >= nreqs) {
           printf("Bad request number: %s\n", id);
           return;
       }

       wait_reqs[n] = reqs[n];
   }

   ret = gai_suspend(wait_reqs, nreqs, NULL);
   if (ret) {
       printf("gai_suspend(): %s\n", gai_strerror(ret));
       return;
   }

   for (int i = 0; i < nreqs; i++) {
       if (wait_reqs[i] == NULL)
           continue;

       ret = gai_error(reqs[i]);
       if (ret == EAI_INPROGRESS)
           continue;

       printf("[%02d] %s: %s\n", i, reqs[i]->ar_name,
              ret == 0 ? "Finished" : gai_strerror(ret));
   }
}

/* Cancel specified requests */
static void
cancel_requests(void)
{
   char *id;
   int ret, n;

   while ((id = strtok(NULL, " ")) != NULL) {
       n = atoi(id);

       if (n >= nreqs) {
           printf("Bad request number: %s\n", id);
           return;
       }

       ret = gai_cancel(reqs[n]);
       printf("[%s] %s: %s\n", id, reqs[atoi(id)]->ar_name,
              gai_strerror(ret));
   }
}

/* List all requests */
static void
list_requests(void)
{
   int ret;
   char host[NI_MAXHOST];
   struct addrinfo *res;

   for (int i = 0; i < nreqs; i++) {
       printf("[%02d] %s: ", i, reqs[i]->ar_name);
       ret = gai_error(reqs[i]);

       if (!ret) {
           res = reqs[i]->ar_result;

           ret = getnameinfo(res->ai_addr, res->ai_addrlen,
                             host, sizeof(host),
                             NULL, 0, NI_NUMERICHOST);
           if (ret) {
               fprintf(stderr, "getnameinfo() failed: %s\n",
                       gai_strerror(ret));
               exit(EXIT_FAILURE);
           }
           puts(host);
       } else {
           puts(gai_strerror(ret));
       }
   }
}

class eai_category_impl:
    public std::error_category
{
public:
    virtual const char * name() const noexcept {
        return "addrinfo";
    }
    virtual std::string message(int ev) const {
        return gai_strerror(ev);
    }
};

eai_category_impl eai_category_instance;

const std::error_category & eai_category()
{
    return eai_category_instance;
}

class eai_code : public std::error_code {
public:
    eai_code(int val):std::error_code(val, eai_category()) {}
    eai_code(gaicb* req):std::error_code(gai_error(req), eai_category()) {}
};

void eai_check(const eai_code& ret, const std::string& from) {
    if (ret) throw std::system_error(ret, from);
}

class Resolver {
public:
    using callback_t = std::function<void(const char*, addrinfo*, const std::error_code&)>;
    static void query(const std::string& name, callback_t callback,const std::string& port="", 
        int family = AF_UNSPEC, int socktype = 0, int protocol = 0, int flags = AI_V4MAPPED | AI_ADDRCONFIG) {
        Resolver* _query = new Resolver(name,callback,port,family,socktype,protocol,flags);
    }
private:

    void on_complete() {
        _callback(_name.c_str(), req.ar_result, eai_code(&req));
        if (req.ar_result) {
            freeaddrinfo(req.ar_result);
        }
        delete this;
    }

    static void notify (sigval v) { ((Resolver*)v.sival_ptr)->on_complete();
    }

    Resolver(const std::string& name, callback_t callback, 
             const std::string& port, int family, int socktype, int protocol, int flags):
             _name(name),_callback(callback),_port(port) {
        hints.ai_family = family;
        hints.ai_socktype = socktype;
        hints.ai_protocol = protocol;
        hints.ai_flags = flags;
        req.ar_name = _name.c_str();
        if (_port.empty()) {
            req.ar_service = 0;
        } else {
            req.ar_service = _port.c_str();
        }
        req.ar_request = &hints;
        se.sigev_notify = SIGEV_THREAD;
        se.sigev_notify_function = Resolver::notify;
        se.sigev_value.sival_ptr = this;
        gaicb * ptr = &req;
        eai_check(getaddrinfo_a(GAI_NOWAIT, &ptr, 1, &se),"getaddrinfo_a");
    }
    std::string _name;
    callback_t _callback;
    std::string _port;
    addrinfo hints;
    gaicb req;
    sigevent se;
};


int
main_old(int argc, char *argv[])
{
   char *cmdline;
   char *cmd;

   while ((cmdline = getcmd()) != NULL) {
       cmd = strtok(cmdline, " ");

       if (cmd == NULL) {
           list_requests();
       } else {
           switch (cmd[0]) {
           case 'a':
               add_requests();
               break;
           case 'w':
               wait_requests();
               break;
           case 'c':
               cancel_requests();
               break;
           case 'l':
               list_requests();
               break;
           default:
               fprintf(stderr, "Bad command: %c\n", cmd[0]);
               break;
           }
       }
   }
   exit(EXIT_SUCCESS);
}

void on_complete(const char* name, addrinfo* ai, const std::error_code& error) {
    if (error) {
        std::cout<<"Get address for "<<name<<": "<<error.message()<<std::endl;
    } else {
        char host[NI_MAXHOST];
        eai_code ec = getnameinfo(ai->ai_addr, ai->ai_addrlen, host, sizeof(host), NULL, 0, NI_NUMERICHOST);
        if (ec) {
           std::cout<<"Get nameinfo error: "<<ec.message()<<std::endl;
        } else {
            std::cout<<name<<" = "<<host<<std::endl;
        }
    }
}

int main(int argc, char *argv[]) {
    Resolver::query("gro-za.org", on_complete);
    usleep(1000000);
    std::cout<<"End"<<std::endl;
}