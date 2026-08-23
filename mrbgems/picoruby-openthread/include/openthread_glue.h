/*
 * The VM-facing half of picoruby-openthread.
 *
 * Nothing here names an OpenThread type. The VM bindings in src/ are
 * compiled by rake from a build config that has no OpenThread include
 * paths at all -- only ports/nrf52/openthread.c is built by the product
 * Makefile, which does. The same split is why picoruby-ble/include/ble.h
 * carries no BTstack types.
 *
 * It also keeps the platform free to change: OT_udp_t is opaque, so no
 * shared code can reach into it and no per-platform layout can drift
 * apart the way picorb_socket_t's did.
 */
#ifndef PICORUBY_OPENTHREAD_H_
#define PICORUBY_OPENTHREAD_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Mirrors otDeviceRole. Kept as its own enum so <openthread/thread.h>
   stays out of the VM bindings; ports/nrf52 static_asserts the pairing. */
typedef enum {
  OT_ROLE_DISABLED = 0,
  OT_ROLE_DETACHED = 1,
  OT_ROLE_CHILD    = 2,
  OT_ROLE_ROUTER   = 3,
  OT_ROLE_LEADER   = 4,
} OT_role_t;

/* An IPv6 address in presentation form, plus room for the NUL. */
#define OT_ADDR_STRLEN 46

/* Largest Active Dataset TLV blob otDatasetSetActiveTlvs accepts. */
#define OT_DATASET_TLVS_MAX 254

/* recvfrom found nothing; not an error, try again later. */
#define OT_RECV_WOULD_BLOCK (-1)
/* The socket is closed. Distinct from the above because a caller that
   blocks until data arrives would otherwise spin forever on a socket
   that can never produce any. */
#define OT_RECV_CLOSED      (-2)

/* ---- lifecycle ---- */
bool OT_init(void);
bool OT_started(void);
bool OT_set_dataset_tlvs(const uint8_t *tlvs, size_t len);
bool OT_set_dataset(const uint8_t key[16], int pan_id, int channel);
bool OT_ip6_enable(bool on);
bool OT_thread_enable(bool on);
int  OT_role(void);
bool OT_eid(char *buf, size_t len);
int  OT_address_count(void);
bool OT_address_at(int index, char *buf, size_t len);

/*
 * Pumping. OT_init registers this as a scheduler service, so the VM
 * drives it on its own; these are exported for the product's idle hook,
 * which has to keep the stack alive while no Ruby task is runnable.
 */
void OT_process(void);
bool OT_work_pending(void);

/* ---- UDP ---- */
typedef struct OT_udp OT_udp_t;

OT_udp_t *OT_udp_open(void);
bool OT_udp_bind(OT_udp_t *sock, int port);
int  OT_udp_sendto(OT_udp_t *sock, const void *data, size_t len,
                   const char *host, int port);
/* Returns the datagram length, or OT_RECV_WOULD_BLOCK. */
int  OT_udp_recvfrom(OT_udp_t *sock, void *buf, size_t maxlen,
                     char *host, size_t hostlen, int *port);
void OT_udp_close(OT_udp_t *sock);

#endif /* PICORUBY_OPENTHREAD_H_ */
