/*
 * checkudpheader.{cc,hh} -- element checks UDP header for correctness
 * (checksums, lengths)
 * Eddie Kohler
 *
 * Counter & handler updates by Georgios Katsikas
 *
 * Copyright (c) 1999-2000 Massachusetts Institute of Technology
 * Copyright (c) 2020 UBITECH and KTH Royal Institute of Technology
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, subject to the conditions
 * listed in the Click LICENSE file. These conditions include: you must
 * preserve this copyright notice, and you cannot mention the copyright
 * holders in advertising related to the Software without their permission.
 * The Software is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This
 * notice is a summary of the Click LICENSE file; the license in that file is
 * legally binding.
 */

#include <click/config.h>
#include "checkudpheader.hh"
#include <clicknet/ip.h>
#include <clicknet/udp.h>
#include <click/glue.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <click/straccum.hh>
CLICK_DECLS

const char *CheckUDPHeader::reason_texts[NREASONS] = {
    "not UDP", "bad packet length", "bad UDP checksum"
};

CheckUDPHeader::CheckUDPHeader() : _reason_drops(0), _checksum(true)
{
    _count = 0;
    _drops = 0;
}

CheckUDPHeader::~CheckUDPHeader()
{
    if (_reason_drops) {
        delete _reason_drops;
    }
}

int
CheckUDPHeader::configure(Vector<String> &conf, ErrorHandler *errh)
{
    bool verbose = false;
    bool details = false;
    bool checksum = true;

    if (Args(conf, this, errh)
        .read("VERBOSE", verbose)
        .read("DETAILS", details)
        .read("CHECKSUM", checksum)
        .complete() < 0)
        return -1;

    _verbose = verbose;
    _checksum = checksum;
    if (details) {
        _reason_drops = new atomic_uint64_t[NREASONS];
        memset(_reason_drops, 0, NREASONS * sizeof(atomic_uint64_t));
    }

    return 0;
}

Packet *
CheckUDPHeader::drop(Reason reason, Packet *p)
{
    if (_drops == 0 || _verbose) {
        click_chatter("UDP header check failed: %s", reason_texts[reason]);
    }
    _drops++;

    if (_reason_drops) {
        _reason_drops[reason]++;
    }

    if (noutputs() == 2) {
        output(1).push(p);
    } else {
        p->kill();
    }

    return 0;
}

Packet *
CheckUDPHeader::simple_action(Packet *p)
{
    const click_ip *iph = p->ip_header();

    if (!p->has_network_header() || iph->ip_p != IP_PROTO_UDP) {
        return drop(NOT_UDP, p);
    }

    const click_udp *udph = p->udp_header();
    unsigned iph_len = iph->ip_hl << 2;
    unsigned len = ntohs(udph->uh_ulen);
    bool is_long = p->length() < len + iph_len + p->network_header_offset();
    if (len < sizeof(click_udp)) {
        return drop(BAD_LENGTH, p);
    }

    if (udph->uh_sum != 0) {
        if (_checksum) {
            unsigned csum = click_in_cksum((unsigned char *)udph, len);
            if (click_in_cksum_pseudohdr(csum, iph, len) != 0) {
                return drop(BAD_CHECKSUM, p);
            }
        }
    }

    _count++;

    return p;
}

String
CheckUDPHeader::read_handler(Element *e, void *thunk)
{
    CheckUDPHeader *c = reinterpret_cast<CheckUDPHeader *>(e);

    switch ((intptr_t)thunk) {
        case h_count: {
            return String(c->_count);
        }
        case h_drops: {
            return String(c->_drops);
        }
        case h_drop_details: {
            StringAccum sa;
            for (unsigned i = 0; i < NREASONS; i++) {
                sa.snprintf(15, "%15" PRIu64, c->_reason_drops[i].value());
                sa << " packets due to: ";
                sa.snprintf(24, "%24s", reason_texts[i]);
                sa << "\n";
            }
            return sa.take_string();
        }
        default: {
            return String("<error>");
        }
    }
}

void
CheckUDPHeader::add_handlers()
{
    add_read_handler("count", read_handler, h_count);
    add_read_handler("drops", read_handler, h_drops);
    if (_reason_drops)
        add_read_handler("drop_details", read_handler, h_drop_details);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(CheckUDPHeader)
ELEMENT_MT_SAFE(CheckUDPHeader)
