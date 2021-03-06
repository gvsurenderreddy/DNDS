/*
 * Dynamic Network Directory Service
 * Copyright (C) 2009-2014
 * Nicolas J. Bouliane <nib@dynvpn.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation; version 3 of the license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details
 */

#include <pthread.h>
#include <unistd.h>

#include <dnds.h>
#include <logger.h>
#include <netbus.h>

#include "context.h"
#include "dnd.h"
#include "dsc.h"
#include "session.h"
#include "tcp.h"

static netc_t *dsc_netc = NULL;
static passport_t *dnd_passport = NULL;
static struct dnd_cfg *dnd_cfg = NULL;

/* TODO extend this tracking table into a subsystem in it's own */
#define MAX_SESSION 1024
struct session *session_tracking_table[MAX_SESSION];
static uint32_t tracking_id = 0;

int transmit_provisioning(struct session *session, char *provCode, uint32_t length)
{
	DNDSMessage_t *msg;

	DNDSMessage_new(&msg);
	DNDSMessage_set_channel(msg, 0);
	DNDSMessage_set_pdu(msg, pdu_PR_dsm);

	/* XXX should have it's own tracking number field  ? */
	DSMessage_set_seqNumber(msg, tracking_id);
	DSMessage_set_ackNumber(msg, 0);
	DSMessage_set_operation(msg, dsop_PR_searchRequest);

	SearchRequest_set_searchType(msg, SearchType_object);

	DNDSObject_t *objNode;
	DNDSObject_new(&objNode);
	DNDSObject_set_objectType(objNode, DNDSObject_PR_node);

	Node_set_contextId(objNode, 0);
	Node_set_provCode(objNode, provCode, length);

	SearchRequest_set_object(msg, objNode);

	net_send_msg(dsc_netc, msg);
	DNDSMessage_del(msg);

	session_tracking_table[tracking_id % MAX_SESSION] = session;
	tracking_id++;

	return 0;
}

int transmit_node_connectinfo(e_ConnectState state, char *ipAddress, char *certName)
{
	DNDSMessage_t *msg;

	DNDSMessage_new(&msg);
	DNDSMessage_set_channel(msg, 0);
	DNDSMessage_set_pdu(msg, pdu_PR_dsm);

        DSMessage_set_seqNumber(msg, 0);
        DSMessage_set_ackNumber(msg, 0);
        DSMessage_set_operation(msg, dsop_PR_nodeConnectInfo);

        NodeConnectInfo_set_certName(msg, certName, strlen(certName));
        NodeConnectInfo_set_ipAddr(msg, ipAddress);
        NodeConnectInfo_set_state(msg, state);

	net_send_msg(dsc_netc, msg);
	DNDSMessage_del(msg);

	return 0;
}

int transmit_search_node()
{
	uint16_t i = 0;
	context_t *context = NULL;

	DNDSObject_t *objNode;

	DNDSMessage_t *msg;
	DNDSMessage_new(&msg);
	DNDSMessage_set_pdu(msg, pdu_PR_dsm);
	DSMessage_set_operation(msg, dsop_PR_searchRequest);
	SearchRequest_set_searchType(msg, SearchType_sequence);

	for (i = 0; i < 4096; i++) {
		context = context_lookup(i);
		if (context) {
			DNDSObject_new(&objNode);
			DNDSObject_set_objectType(objNode, DNDSObject_PR_node);
			Node_set_contextId(objNode, i);
			SearchRequest_add_to_objects(msg, objNode);
		}
	}

	net_send_msg(dsc_netc, msg);
	DNDSMessage_del(msg);

	return 0;
}

static void on_secure(netc_t *netc)
{
	jlog(L_DEBUG, "connection secured with DSD");

	DNDSMessage_t *msg;

	DNDSMessage_new(&msg);
	DNDSMessage_set_channel(msg, 0);
	DNDSMessage_set_pdu(msg, pdu_PR_dsm);

	DSMessage_set_seqNumber(msg, 0);
	DSMessage_set_ackNumber(msg, 0);
	DSMessage_set_operation(msg, dsop_PR_searchRequest);

	SearchRequest_set_searchType(msg, SearchType_all);
	SearchRequest_set_objectName(msg, ObjectName_context);

	net_send_msg(netc, msg);
	DNDSMessage_del(msg);
}

static void delRequest(DNDSMessage_t *msg)
{
	DNDSObject_t *object;
	DelRequest_get_object(msg, &object);

	uint32_t contextId = -1;
	Node_get_contextId(object, &contextId);

	size_t length = 0;
	char *uuid = NULL;
	Node_get_uuid(object, &uuid, &length);

	context_t *context = NULL;
	context = context_lookup(contextId);

	if (context == NULL) {
		jlog(L_ERROR, "context id {%d} doesn't exist");
		return;
	}

	/* remove the node from the access table */
	ctable_erase(context->atable, uuid);

	/* if the node is connected, mark it to be purged */
	struct session *session = NULL;
	session = ctable_find(context->ctable, uuid);
	if (session) {
		session->state = SESSION_STATE_PURGE;
	}
}

static void handle_SearchResponse_Node(DNDSMessage_t *msg)
{
	struct session *session;
	uint32_t tracked_id;
	DNDSObject_t *object;
	uint32_t count; int ret;
	size_t length;

	char *certificate = NULL;
	uint8_t *certificateKey = NULL;
	uint8_t *trustedCert = NULL;
        char ipAddress[INET_ADDRSTRLEN];

	SearchResponse_get_object_count(msg, &count);

	DNDSMessage_t *new_msg;
	DNDSMessage_new(&new_msg);
	DNDSMessage_set_channel(new_msg, 0);
	DNDSMessage_set_pdu(new_msg, pdu_PR_dnm);

	DNMessage_set_operation(new_msg, dnop_PR_provResponse);

	ret = SearchResponse_get_object(msg, &object);
	if (ret == DNDS_success && object != NULL) {

		Node_get_certificate(object, &certificate, &length);
		ProvResponse_set_certificate(new_msg, certificate, length);

		Node_get_certificateKey(object, &certificateKey, &length);
		ProvResponse_set_certificateKey(new_msg, certificateKey, length);

		Node_get_trustedCert(object, &trustedCert, &length);
		ProvResponse_set_trustedCert(new_msg, trustedCert, length);

		Node_get_ipAddress(object, ipAddress);
		ProvResponse_set_ipAddress(new_msg, ipAddress);
	}

	DSMessage_get_seqNumber(msg, &tracked_id);

	session = session_tracking_table[tracked_id % MAX_SESSION];
	session_tracking_table[tracked_id % MAX_SESSION] = NULL;
	if (session)
		net_send_msg(session->netc, new_msg);
	DNDSMessage_del(new_msg);
}

static void handle_SearchResponse_node(DNDSMessage_t *msg)
{
	DNDSObject_t *object;
	int ret;
	size_t length;
	uint32_t count;
	char *uuid;
	uint32_t contextId;
	context_t *context;

	SearchResponse_get_object_count(msg, &count);
	while (count-- > 0) {
		ret = SearchResponse_get_object(msg, &object);
		if (ret == DNDS_success && object != NULL) {

			Node_get_uuid(object, &uuid, &length);
			Node_get_contextId(object, &contextId);

			context = context_lookup(contextId);
			if (context) {
				ctable_insert(context->atable, uuid, context->access_session);

			}
		}
	}
}

static void handle_SearchResponse_Context(DNDSMessage_t *msg)
{
	DNDSObject_t *object;
	uint32_t count;
	int ret;
	size_t length;
	uint32_t id;
	e_Topology topology;
	char *desc;
	char network[INET_ADDRSTRLEN];
	char netmask[INET_ADDRSTRLEN];
	char *serverCert;
	char *serverPrivkey;
	char *trustedCert;

	SearchResponse_get_object_count(msg, &count);
	while (count-- > 0) {

		ret = SearchResponse_get_object(msg, &object);
		if (ret == DNDS_success && object != NULL) {

			Context_get_id(object, &id);
			Context_get_topology(object, &topology);
			Context_get_description(object, &desc, &length);
			Context_get_network(object, network);
			Context_get_netmask(object, netmask);
			Context_get_serverCert(object, &serverCert, &length);
			Context_get_serverPrivkey(object, &serverPrivkey, &length);
			Context_get_trustedCert(object, &trustedCert, &length);

			context_create(id, network, netmask, serverCert, serverPrivkey, trustedCert);

			DNDSObject_del(object);
		}
	}
}

static void handle_SearchResponse(DNDSMessage_t *msg)
{
	e_SearchType SearchType;

	SearchResponse_get_searchType(msg, &SearchType);

	if (SearchType == SearchType_all) {
		handle_SearchResponse_Context(msg);
		transmit_search_node();
	}

	if (SearchType == SearchType_sequence) {
		handle_SearchResponse_node(msg);
		dnd_cfg->dsc_initialized = 1;
	}

	if (SearchType == SearchType_object) {
		handle_SearchResponse_Node(msg);
	}
}

static void dispatch_operation(DNDSMessage_t *msg)
{
	dsop_PR operation;
	DSMessage_get_operation(msg, &operation);

	switch (operation) {
	case dsop_PR_delRequest:
		delRequest(msg);
		break;

	case dsop_PR_searchResponse:
		handle_SearchResponse(msg);
		break;

	default:
		jlog(L_WARNING, "not a valid DSM operation");
	}
}

static void on_input(netc_t *netc)
{
	DNDSMessage_t *msg;
	mbuf_t **mbuf_itr;
	pdu_PR pdu;

	mbuf_itr = &netc->queue_msg;

	while (*mbuf_itr != NULL) {

		msg = (DNDSMessage_t *)(*mbuf_itr)->ext_buf;
		DNDSMessage_get_pdu(msg, &pdu);

		switch (pdu) {
		case pdu_PR_dsm:	/* DNDS protocol */
			dispatch_operation(msg);
			break;
		default:
			/* TODO disconnect session */
			jlog(L_ERROR, "invalid PDU");
			break;
		}

		mbuf_del(mbuf_itr, *mbuf_itr);
	}
}

static void on_disconnect(netc_t *netc)
{
	(void)(netc);

	if (dnd_cfg->dsc_running == 0) {
		return;
	}

	netc_t *retry_netc = NULL;

	jlog(L_NOTICE, "disconnected from dsd");
	jlog(L_NOTICE, "connection retry to dsd...");
	do {
		sleep(5);
		retry_netc = net_client(dnd_cfg->dsd_ipaddr, dnd_cfg->dsd_port,
		    NET_PROTO_TCP, NET_SECURE_RSA, dnd_passport,
		    on_disconnect, on_input, on_secure);
	} while (retry_netc == NULL);

	dsc_netc = retry_netc;
}

static void *dsc_loop(void *nil)
{
	(void)nil;

	while (dnd_cfg->dsc_running) {
		tcpbus_ion_poke();
	}

	return NULL;
}

int dsc_init(struct dnd_cfg *cfg)
{
	dnd_cfg = cfg;
	dnd_cfg->dsc_running = 1;

	jlog(L_NOTICE, "dsc initializing...");

	dnd_passport = pki_passport_load_from_file(dnd_cfg->certificate, dnd_cfg->privatekey, dnd_cfg->trusted_cert);
	dsc_netc = net_client(dnd_cfg->dsd_ipaddr, dnd_cfg->dsd_port, NET_PROTO_TCP, NET_SECURE_RSA, dnd_passport,
				on_disconnect, on_input, on_secure);

	if (dsc_netc == NULL) {
		jlog(L_NOTICE, "failed to connect to the Directory Service");
		return -1;
	}

	pthread_t thread_loop;
	pthread_attr_t attr;

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	pthread_create(&thread_loop, &attr, dsc_loop, NULL);

	return 0;
}

void dsc_fini()
{
	net_disconnect(dsc_netc);
	pki_passport_destroy(dnd_passport);
	context_free();
}
