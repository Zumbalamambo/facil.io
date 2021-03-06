/**
This example emulates the websocket shootout testing requirements, except that
the JSON will not be fully parsed.

See the Websocket-Shootout repository at GitHub:
https://github.com/hashrocket/websocket-shootout

Using the benchmarking tool, try the following benchmarks (binary and text):

websocket-bench broadcast ws://127.0.0.1:3000/ --concurrent 10 \
--sample-size 100 --server-type binary --step-size 1000 --limit-percentile 95 \
--limit-rtt 250ms --initial-clients 1000

websocket-bench broadcast ws://127.0.0.1:3000/ --concurrent 10 \
--sample-size 100 --step-size 1000 --limit-percentile 95 \
--limit-rtt 250ms --initial-clients 1000

*/

#include "spnlock.h"
#include "websockets.h"

#include <string.h>

#ifdef __APPLE__
#include <dlfcn.h>
#endif

FIOBJ CHANNEL_TEXT;
FIOBJ CHANNEL_BINARY;

static size_t sub_count;
static size_t unsub_count;

static void on_websocket_unsubscribe(void *udata) {
  (void)udata;
  spn_add(&unsub_count, 1);
}

static void print_subscription_balance(void *a) {
  fprintf(stderr, "* subscribe / on_unsubscribe count (%s): %zu / %zu\n",
          (char *)a, sub_count, unsub_count);
}

static void on_open_shootout_websocket(ws_s *ws) {
  spn_add(&sub_count, 2);
  websocket_subscribe(ws, .channel = CHANNEL_TEXT, .force_text = 1,
                      .on_unsubscribe = on_websocket_unsubscribe);
  websocket_subscribe(ws, .channel = CHANNEL_BINARY, .force_binary = 1,
                      .on_unsubscribe = on_websocket_unsubscribe);
}
static void on_open_shootout_websocket_sse(http_sse_s *sse) {
  http_sse_subscribe(sse, .channel = CHANNEL_TEXT);
}

static void handle_websocket_messages(ws_s *ws, char *data, size_t size,
                                      uint8_t is_text) {
  (void)(ws);
  (void)(is_text);
  (void)(size);
  if (data[0] == 'b') {
    FIOBJ msg = fiobj_str_new(data, size);
    facil_publish(.channel = CHANNEL_BINARY, .message = msg);
    fiobj_free(msg);
    // fwrite(".", 1, 1, stderr);
    data[0] = 'r';
    websocket_write(ws, data, size, 0);
  } else if (data[9] == 'b') {
    // fwrite(".", 1, 1, stderr);
    FIOBJ msg = fiobj_str_new(data, size);
    facil_publish(.channel = CHANNEL_TEXT, .message = msg);
    fiobj_free(msg);
    /* send result */
    size = size + (25 - 19);
    void *buff = malloc(size);
    memcpy(buff, "{\"type\":\"broadcastResult\"", 25);
    memcpy((void *)(((uintptr_t)buff) + 25), data + 19, size - 25);
    websocket_write(ws, buff, size, 1);
    free(buff);
  } else {
    /* perform echo */
    websocket_write(ws, data, size, is_text);
  }
}

static void answer_http_request(http_s *request) {
  http_set_header(request, HTTP_HEADER_CONTENT_TYPE,
                  http_mimetype_find("txt", 3));
  http_send_body(request, "This is a Websocket-Shootout example!", 37);
}
static void answer_http_upgrade(http_s *request, char *target, size_t len) {
  if (len >= 9 && target[1] == 'e') {
    http_upgrade2ws(request, .on_message = handle_websocket_messages,
                    .on_open = on_open_shootout_websocket);
  } else if (len >= 3 && target[0] == 's') {
    http_upgrade2sse(request, .on_open = on_open_shootout_websocket_sse);
  } else
    http_send_error(request, 400);
}

#include "fio_cli.h"
/*
Read available command line details using "-?".
*/
int main(int argc, char const *argv[]) {
  const char *port = "3000";
  const char *public_folder = NULL;
  uint32_t threads = 0;
  uint32_t workers = 0;
  uint8_t print_log = 0;
  // allocate global resources
  CHANNEL_TEXT = fiobj_str_new("CHANNEL_TEXT", 12);
  CHANNEL_BINARY = fiobj_str_new("CHANNEL_BINARY", 14);

  /*     ****  Command line arguments ****     */
  fio_cli_start(
      argc, argv, 0,
      "This is a facil.io example application.\n"
      "\nThis example conforms to the "
      "Websocket Shootout requirements at:\n"
      "https://github.com/hashrocket/websocket-shootout\n"
      "\nThe following arguments are supported:",
      "-threads -t The number of threads to use. System dependent default.",
      FIO_CLI_TYPE_INT,
      "-workers -w The number of processes to use. System dependent default.",
      FIO_CLI_TYPE_INT, "-port -p The port number to listen to.",
      FIO_CLI_TYPE_INT,
      "-public -www A public folder for serve an HTTP static file service.",
      "-log -v Turns logging on.", FIO_CLI_TYPE_BOOL,
      "-optimize -o Turns WebSocket broadcast optimizations on.",
      FIO_CLI_TYPE_BOOL);

  if (fio_cli_get("-p"))
    port = fio_cli_get("-p");
  if (fio_cli_get("-www")) {
    public_folder = fio_cli_get("-www");
    fprintf(stderr, "* serving static files from:%s\n", public_folder);
  }
  if (fio_cli_get_i("-t"))
    threads = fio_cli_get_i("-t");
  if (fio_cli_get_i("-w"))
    workers = fio_cli_get_i("-w");
  print_log = fio_cli_get_i("-v");

  /* optimize websocket pub/sub for multi-client broadcasts */
  if (fio_cli_get_i("-o")) {
    fprintf(stderr, "* Turning on WebSocket broadcast optimizations.\n");
    websocket_optimize4broadcasts(WEBSOCKET_OPTIMIZE_PUBSUB, 1);
    websocket_optimize4broadcasts(WEBSOCKET_OPTIMIZE_PUBSUB_TEXT, 1);
    websocket_optimize4broadcasts(WEBSOCKET_OPTIMIZE_PUBSUB_BINARY, 1);
  }
  fio_cli_end();

  /*     ****  actual code ****     */
  if (http_listen(port, NULL, .on_request = answer_http_request,
                  .on_upgrade = answer_http_upgrade, .log = print_log,
                  .public_folder = public_folder) == -1) {
    perror("Couldn't initiate Websocket Shootout service");
    exit(1);
  }

#ifdef __APPLE__
  /* patch for dealing with the High Sierra `fork` limitations */
  void *obj_c_runtime = dlopen("Foundation.framework/Foundation", RTLD_LAZY);
  (void)obj_c_runtime;
#endif

#if DEBUG
  facil_core_callback_add(FIO_CALL_ON_SHUTDOWN, print_subscription_balance,
                          "on shutdown");
  facil_core_callback_add(FIO_CALL_ON_FINISH, print_subscription_balance,
                          "on finish");
  facil_core_callback_add(FIO_CALL_AT_EXIT, print_subscription_balance,
                          "at exit");
#endif

  facil_run(.threads = threads, .processes = workers);
  // free global resources.
  fiobj_free(CHANNEL_TEXT);
  fiobj_free(CHANNEL_BINARY);
}
