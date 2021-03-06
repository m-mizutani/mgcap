#ifndef __LIBMGCAP_H__
#define __LIBMGCAP_H__

#include <stdint.h>

typedef struct mgcap_ mgcap_t;

struct mgcap_hdr_ {
	uint16_t pktlen_;
	uint64_t timestamp_;
};

typedef struct mgcap_hdr_ mgcap_hdr;

mgcap_t* new_mgcap();
void destroy_mgcap(mgcap_t* mg);
int mgcap_set_device(mgcap_t* mg, const char* dev_name);
int mgcap_next(mgcap_t* mg, void** pktptr, mgcap_hdr* hdr);

#endif   // __LIBMGCAP_H__
