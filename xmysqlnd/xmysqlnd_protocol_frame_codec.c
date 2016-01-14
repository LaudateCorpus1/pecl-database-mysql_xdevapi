/*
  +----------------------------------------------------------------------+
  | PHP Version 7                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 2006-2015 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Authors: Andrey Hristov <andrey@mysql.com>                           |
  +----------------------------------------------------------------------+
*/
#include "php.h"
#include "ext/mysqlnd/mysqlnd.h"
#include "ext/mysqlnd/mysqlnd_connection.h"
#include "ext/mysqlnd/mysqlnd_priv.h"
#include "ext/mysqlnd/mysqlnd_wireprotocol.h"
#include "ext/mysqlnd/mysqlnd_debug.h"
#include "xmysqlnd.h"
#include "xmysqlnd_priv.h" // XMYSQLND_INC_SESSION_STATISTIC_W_VALUE3
#include "xmysqlnd_protocol_frame_codec.h"
#include "xmysqlnd_wireprotocol.h"
#include "xmysqlnd_protocol_dumper.h"
#include "xmysqlnd_driver.h"

#define XMYSQLND_PACKET_TYPE_SIZE	1
#define XMYSQLND_PACKET_TYPE_STORE	int1store
#define XMYSQLND_PACKET_TYPE_LOAD	uint1korr

#define XMYSQLND_PAYLOAD_LENGTH_SIZE 	4
#define XMYSQLND_PAYLOAD_LENGTH_STORE	int4store
#define XMYSQLND_PAYLOAD_LENGTH_LOAD 	uint4korr

/* {{{ xmysqlnd_pfc::reset */
static enum_func_status
XMYSQLND_METHOD(xmysqlnd_pfc, reset)(XMYSQLND_PFC * const pfc, MYSQLND_STATS * const stats, MYSQLND_ERROR_INFO * const error_info)
{
	DBG_ENTER("xmysqlnd_pfc::reset");
	DBG_RETURN(PASS);
}
/* }}} */


/* {{{ xmysqlnd_pfc::send */
static enum_func_status
XMYSQLND_METHOD(xmysqlnd_pfc, send)(XMYSQLND_PFC * const pfc,
									MYSQLND_VIO * const vio,
									zend_uchar packet_type,
									const zend_uchar * const buffer,
									const size_t count,
									size_t * bytes_sent,
									MYSQLND_STATS * const stats,
									MYSQLND_ERROR_INFO * const error_info)
{
	zend_uchar header[XMYSQLND_PAYLOAD_LENGTH_SIZE + XMYSQLND_PACKET_TYPE_SIZE];
	size_t packets_sent = 1;
	size_t left = buffer? count: 0;
	const zend_uchar * p = (zend_uchar *) buffer;
	size_t to_be_sent;

	DBG_ENTER("xmysqlnd_pfc::send");
	DBG_INF_FMT("count=" MYSQLND_SZ_T_SPEC, count);
	if (!vio || FALSE == vio->data->m.has_valid_stream(vio)) {
		DBG_RETURN(FAIL);
	}

#ifdef PHP_DEBUG
	xmysqlnd_dump_client_message(packet_type, buffer, count);
#endif

	*bytes_sent = 0;
	do {
		to_be_sent = MIN(left, pfc->data->max_packet_size - 1);
		DBG_INF_FMT("to_be_sent=%u", to_be_sent);
		DBG_INF_FMT("packets_sent=%u", packets_sent);
		{
			/* the packet type is part of the payload so we have always + 1 and never 0 as length */
			XMYSQLND_PAYLOAD_LENGTH_STORE(header, to_be_sent + XMYSQLND_PACKET_TYPE_SIZE);
			XMYSQLND_PACKET_TYPE_STORE(header + XMYSQLND_PAYLOAD_LENGTH_SIZE, packet_type);
			bytes_sent += vio->data->m.network_write(vio, header, XMYSQLND_PAYLOAD_LENGTH_SIZE + XMYSQLND_PACKET_TYPE_SIZE, stats, error_info);
			if (to_be_sent) { /* the first packet can be empty, only packet_type */
				bytes_sent += vio->data->m.network_write(vio, p, to_be_sent, stats, error_info);
			}
		}
		p += to_be_sent;
		left -= to_be_sent;
		packets_sent++;
	} while (bytes_sent && (left > 0 || to_be_sent == pfc->data->max_packet_size));


	XMYSQLND_INC_SESSION_STATISTIC_W_VALUE3(stats,
			XMYSQLND_STAT_BYTES_SENT, count + packets_sent * (XMYSQLND_PAYLOAD_LENGTH_SIZE + XMYSQLND_PACKET_TYPE_SIZE),
			XMYSQLND_STAT_PROTOCOL_OVERHEAD_OUT, packets_sent * (XMYSQLND_PAYLOAD_LENGTH_SIZE + XMYSQLND_PACKET_TYPE_SIZE),
			XMYSQLND_STAT_PACKETS_SENT, packets_sent);

	/* Even for zero size payload we have to send a packet */
	if (!bytes_sent) {
		DBG_ERR_FMT("Can't %u send bytes", count);
		SET_CLIENT_ERROR(error_info, CR_SERVER_GONE_ERROR, UNKNOWN_SQLSTATE, mysqlnd_server_gone);
		DBG_RETURN(FAIL);
	}
	DBG_RETURN(PASS);
}
/* }}} */


/* {{{ xmysqlnd_pfc::receive */
static enum_func_status
XMYSQLND_METHOD(xmysqlnd_pfc, receive)(XMYSQLND_PFC * const pfc,
									   MYSQLND_VIO * const vio,
									   zend_uchar * prealloc_buffer,
									   const size_t prealloc_buffer_len,
									   zend_uchar * packet_type,
									   zend_uchar ** buffer,
									   size_t * count,
									   MYSQLND_STATS * const stats,
									   MYSQLND_ERROR_INFO * const error_info)
{
	zend_uchar header[XMYSQLND_PAYLOAD_LENGTH_SIZE + XMYSQLND_PACKET_TYPE_SIZE];
	size_t packets_received = 1;

	DBG_ENTER("xmysqlnd_pfc::receive");
	if (!vio || FALSE == vio->data->m.has_valid_stream(vio)) {
		DBG_INF("FAIL");
		DBG_RETURN(FAIL);
	}
	if (PASS == vio->data->m.network_read(vio, header, XMYSQLND_PAYLOAD_LENGTH_SIZE + XMYSQLND_PACKET_TYPE_SIZE, stats, error_info)) {
		*packet_type = XMYSQLND_PACKET_TYPE_LOAD(header + XMYSQLND_PAYLOAD_LENGTH_SIZE);
		*count = XMYSQLND_PAYLOAD_LENGTH_LOAD(header) - XMYSQLND_PACKET_TYPE_SIZE;
		if (*count > prealloc_buffer_len || !prealloc_buffer) {
			*buffer = (zend_uchar*) mnd_emalloc(*count);
		} else {
			*buffer = prealloc_buffer;
		}
		if (PASS == vio->data->m.network_read(vio, *buffer, *count, stats, error_info)) {
#ifdef PHP_DEBUG
			xmysqlnd_dump_server_message(*packet_type, *buffer, *count);
#endif
			XMYSQLND_INC_SESSION_STATISTIC_W_VALUE3(stats,
				XMYSQLND_STAT_BYTES_RECEIVED, count + packets_received * (XMYSQLND_PAYLOAD_LENGTH_SIZE + XMYSQLND_PACKET_TYPE_SIZE),
				XMYSQLND_STAT_PROTOCOL_OVERHEAD_IN, packets_received * (XMYSQLND_PAYLOAD_LENGTH_SIZE + XMYSQLND_PACKET_TYPE_SIZE),
				XMYSQLND_STAT_PACKETS_RECEIVED, packets_received);

			DBG_INF("PASS");
			DBG_RETURN(PASS);
		}
	}
	DBG_INF("FAIL");
	DBG_RETURN(FAIL);
}
/* }}} */


/* {{{ xmysqlnd_pfc::set_client_option */
static enum_func_status
XMYSQLND_METHOD(xmysqlnd_pfc, set_client_option)(XMYSQLND_PFC * const pfc, enum_xmysqlnd_client_option option, const char * const value)
{
	DBG_ENTER("xmysqlnd_pfc::set_client_option");
	DBG_INF_FMT("option=%u", option);
	switch (option) {
		default:
			DBG_RETURN(FAIL);
	}
	DBG_RETURN(PASS);
}
/* }}} */


/* {{{ xmysqlnd_pfc::free_contents */
static void
XMYSQLND_METHOD(xmysqlnd_pfc, free_contents)(XMYSQLND_PFC * pfc)
{
	DBG_ENTER("xmysqlnd_pfc::free_contents");

	DBG_VOID_RETURN;
}
/* }}} */


/* {{{ xmysqlnd_pfc::init */
static enum_func_status
XMYSQLND_METHOD(xmysqlnd_pfc, init)(XMYSQLND_PFC * const pfc, MYSQLND_CLASS_METHODS_TYPE(xmysqlnd_object_factory) *object_factory, MYSQLND_STATS * const stats, MYSQLND_ERROR_INFO * const error_info)
{
	return PASS;
}
/* }}} */


/* {{{ xmysqlnd_pfc::dtor */
static void
XMYSQLND_METHOD(xmysqlnd_pfc, dtor)(XMYSQLND_PFC * const pfc, MYSQLND_STATS * const stats, MYSQLND_ERROR_INFO * const error_info)
{
	DBG_ENTER("xmysqlnd_pfc::dtor");
	if (pfc) {
		pfc->data->m.free_contents(pfc);

		mnd_pefree(pfc->data, pfc->data->persistent);
		mnd_pefree(pfc, pfc->persistent);
	}
	DBG_VOID_RETURN;
}
/* }}} */


MYSQLND_CLASS_METHODS_START(xmysqlnd_protocol_packet_frame_codec)
	XMYSQLND_METHOD(xmysqlnd_pfc, init),
	XMYSQLND_METHOD(xmysqlnd_pfc, reset),

	XMYSQLND_METHOD(xmysqlnd_pfc, set_client_option),

	XMYSQLND_METHOD(xmysqlnd_pfc, send),
	XMYSQLND_METHOD(xmysqlnd_pfc, receive),

	XMYSQLND_METHOD(xmysqlnd_pfc, free_contents),
	XMYSQLND_METHOD(xmysqlnd_pfc, dtor),
MYSQLND_CLASS_METHODS_END;


/* {{{ xmysqlnd_pfc_init */
PHPAPI XMYSQLND_PFC *
xmysqlnd_pfc_init(const zend_bool persistent, MYSQLND_CLASS_METHODS_TYPE(xmysqlnd_object_factory) *object_factory, MYSQLND_STATS * stats, MYSQLND_ERROR_INFO * error_info)
{
	MYSQLND_CLASS_METHODS_TYPE(xmysqlnd_object_factory) *factory = object_factory? object_factory : &MYSQLND_CLASS_METHOD_TABLE_NAME(xmysqlnd_object_factory);
	XMYSQLND_PFC * pfc = NULL;
	DBG_ENTER("xmysqlnd_pfc_init");
	pfc = factory->get_protocol_frame_codec(factory, persistent, stats, error_info);
	DBG_RETURN(pfc);
}
/* }}} */


/* {{{ xmysqlnd_pfc_free */
PHPAPI void
xmysqlnd_pfc_free(XMYSQLND_PFC * const pfc, MYSQLND_STATS * stats, MYSQLND_ERROR_INFO * error_info)
{
	DBG_ENTER("xmysqlnd_pfc_free");
	if (pfc) {
		pfc->data->m.dtor(pfc, stats, error_info);
	}
	DBG_VOID_RETURN;
}
/* }}} */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
