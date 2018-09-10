#ifndef H_FIOBJ4SOCK_H
#define H_FIOBJ4SOCK_H
/**
 * Defines a helper for using fiobj with the sock library.
 */

#include <fio.h>
#include <fiobj.h>

static void fiobj4sock_dealloc(void *o) { fiobj_free((FIOBJ)o); }

/** send a FIOBJ  object through a socket. */
static inline __attribute__((unused)) ssize_t fiobj_send_free(intptr_t uuid,
                                                              FIOBJ o) {
  fio_cstr_s s = fiobj_obj2cstr(o);
  return fio_write2(uuid, .data.buffer = (void *)(o),
                    .offset = (((intptr_t)s.data) - ((intptr_t)(o))),
                    .length = s.length, .after.dealloc = fiobj4sock_dealloc);
}

#endif