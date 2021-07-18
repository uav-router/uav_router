//clang++ others/sentry.cpp -o bintest/sentrytest -I dependencies/sentry-native-0.4.10/install/include -std=c++11 -Ldependencies/sentry-native-0.4.10/install/lib64 -lsentry
#include <sentry.h>

int main(void) {
  sentry_options_t *options = sentry_options_new();
  sentry_options_set_dsn(options, "https://657e42f753e64d4591e29f034782803d@o884048.ingest.sentry.io/5837073");
  sentry_init(options);

  sentry_capture_event(sentry_value_new_message_event(
  /*   level */ SENTRY_LEVEL_INFO,
  /*  logger */ "custom",
  /* message */ "It works!"
    ));

  // make sure everything flushes
  sentry_close();
}