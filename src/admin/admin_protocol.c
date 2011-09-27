/*
    4store - a clustered RDF storage and query engine

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.


    Copyright 2011 Dave Challis
 */

#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>

#include "admin_protocol.h"

/* returns empty packet with headers set */
static unsigned char *init_packet(int cmd, int len)
{
    /* constant header bytes */
    static char* header = "AC";
    static uint8_t vers = (uint8_t)ADM_PROTO_VERS;

    int n;         /* bytes to write */
    int total = 0; /* total bytes written */

    uint8_t adm_cmd = (uint8_t)cmd;    /* command as 1 byte */
    uint16_t data_len = (uint16_t)len; /* length of data as 2 bytes */

    int buf_len = ADM_HEADER_LEN + len;
    unsigned char *buf = (unsigned char *)malloc(buf_len);
    if (buf == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    unsigned char *p = buf; /* current location in buffer */

    n = ADM_H_LEN;
    memcpy(p, header, n);
    p += n;
    total += n;

    n = ADM_H_VERS_LEN;
    memcpy(p, &vers, n);
    p += n;
    total += n;

    n = ADM_H_CMD_LEN;
    memcpy(p, &adm_cmd, n);
    p += n;
    total += n;

    n = ADM_H_DL_LEN;
    memcpy(p, &data_len, n);
    p += n;
    total += n;

    /* zero remaining buffer */
    memset(p, 0, len);

    return buf;
}

static unsigned char *fsap_encode_cmd_no_params(int cmd)
{
    return init_packet(cmd, 0);
}

int fsap_decode_header(const unsigned char *buf, uint8_t *cmd, uint16_t *size)
{
    const unsigned char *p = buf;

    /* check that header starts with "AC" */
    if (p[0] != 'A' || p[1] != 'C') {
        errno = ADM_ERR_PROTO;
        return -1;
    }
    p += ADM_H_LEN;

    /* check that matching protocol versions are used */
    if (*p != ADM_PROTO_VERS) {
        errno = ADM_ERR_PROTO_VERS;
        return -1;
    }
    p += ADM_H_VERS_LEN;

    memcpy(cmd, p, ADM_H_CMD_LEN);
    p += ADM_H_CMD_LEN;

    memcpy(size, p, ADM_H_DL_LEN);
    p += ADM_H_DL_LEN;

    return 0;
}

void fsap_set_command_bytes(unsigned char *buf, uint8_t new_cmd)
{
    /* move to start of command section of buffer */
    unsigned char *p = buf + ADM_H_LEN + ADM_H_VERS_LEN;

    /* overwrite old command value */
    memcpy(p, &new_cmd, ADM_H_CMD_LEN);
}

/* convenience function for common return type.
   Returns rv it was encoded with, sets kb_name and msg to strings that
   need freeing. */
static fsa_kb_response *decode_kb_response(const unsigned char *buf)
{
    uint8_t kb_name_len;
    uint16_t msg_len;

    fsa_kb_response *kbr = fsa_kb_response_new();

    memcpy(&(kbr->return_val), buf, sizeof(uint8_t));
    buf += sizeof(uint8_t);

    memcpy(&kb_name_len, buf, sizeof(uint8_t));
    buf += sizeof(uint8_t);

    memcpy(&msg_len, buf, sizeof(uint16_t));
    buf += sizeof(uint16_t);

    if (kb_name_len > 0) {
        kbr->kb_name = (unsigned char *)malloc(kb_name_len + 1);
        memcpy(kbr->kb_name, buf, kb_name_len);
        kbr->kb_name[kb_name_len] = '\0';
        buf += kb_name_len;
    }

    if (msg_len > 0) {
        kbr->msg = (unsigned char *)malloc(msg_len + 1);
        memcpy(kbr->msg, buf, msg_len);
        kbr->msg[msg_len] = '\0';
        buf += msg_len;
    }

    return kbr;
}

/* convenience function for common return type. */
static unsigned char *encode_kb_response(int rsp, int retval,
                                         const unsigned char *kb_name,
                                         const unsigned char *msg,
                                         int *len)
{
    uint8_t return_val = (uint8_t)retval;
    uint8_t kb_name_len = 0;
    uint16_t msg_len = 0;

    /* encode kb_name length into 1 byte */
    if (kb_name != NULL) {
        int len = strlen((char *)kb_name);
        if (len > 255) {
            /* too long to encode */
            return NULL;
        }
        kb_name_len = (uint8_t)len;
    }

    /* encode msg length into 2 bytes */
    if (msg != NULL) {
        int len = strlen((char *)msg);
        if (len > 65535) {
            /* too long to encode */
            return NULL;
        }
        msg_len = (uint16_t)len;
    }

    int data_len = (2 * sizeof(uint8_t))
                 + sizeof(uint16_t)
                 + kb_name_len
                 + msg_len
                 ;
    unsigned char *buf = init_packet(rsp, data_len);
    unsigned char *p = buf;
    p += ADM_HEADER_LEN;

    memcpy(p, &return_val, sizeof(uint8_t));
    p += sizeof(uint8_t);

    memcpy(p, &kb_name_len, sizeof(uint8_t));
    p += sizeof(uint8_t);

    memcpy(p, &msg_len, sizeof(uint16_t));
    p += sizeof(uint16_t);

    if (kb_name_len > 0) {
        memcpy(p, kb_name, kb_name_len);
        p += kb_name_len;
    }

    if (msg_len > 0) {
        memcpy(p, msg, msg_len);
        p += msg_len;
    }

    *len = ADM_HEADER_LEN + data_len;
    return buf;
}

/* expect n more messages */
unsigned char *fsap_encode_rsp_expect_n(int n, int *len)
{
    uint16_t n_messages = (uint16_t)n;
    int data_len = sizeof(uint16_t);
    unsigned char *buf = init_packet(ADM_RSP_EXPECT_N, data_len);
    unsigned char *p = buf;
    p += ADM_HEADER_LEN;

    memcpy(p, &n_messages, data_len);
    *len = data_len + ADM_HEADER_LEN;
    return buf;
}

int fsap_decode_rsp_expect_n(const unsigned char *buf)
{
    uint16_t n_messages;
    memcpy(&n_messages, buf, sizeof(uint16_t));
    return (int)n_messages;
}

unsigned char *fsap_encode_cmd_stop_kb_all(int *len)
{
    *len = ADM_HEADER_LEN;
    return fsap_encode_cmd_no_params(ADM_CMD_STOP_KB_ALL);
}

unsigned char *fsap_encode_cmd_stop_kb(const unsigned char *kb_name, int *len)
{
    int data_len = strlen((char *)kb_name);
    unsigned char *buf = init_packet(ADM_CMD_STOP_KB, data_len);
    unsigned char *p = buf;
    p += ADM_HEADER_LEN; /* move to start of data section */

    memcpy(p, kb_name, data_len);
    *len = data_len + ADM_HEADER_LEN;
    return buf;
}

unsigned char *fsap_encode_rsp_stop_kb(int retval,
                                       const unsigned char *kb_name,
                                       const unsigned char *msg,
                                       int *len)
{
    return encode_kb_response(ADM_RSP_STOP_KB, retval, kb_name, msg, len);
}

fsa_kb_response *fsap_decode_rsp_stop_kb(const unsigned char *buf)
{
    return decode_kb_response(buf);
}

unsigned char *fsap_encode_cmd_start_kb_all(int *len)
{
    *len = ADM_HEADER_LEN;
    return fsap_encode_cmd_no_params(ADM_CMD_START_KB_ALL);
}

unsigned char *fsap_encode_cmd_start_kb(const unsigned char *kb_name,
                                        int *len)
{
    int data_len = strlen((char *)kb_name);
    unsigned char *buf = init_packet(ADM_CMD_START_KB, data_len);
    unsigned char *p = buf;
    p += ADM_HEADER_LEN; /* move to start of data section */

    memcpy(p, kb_name, data_len);
    *len = data_len + ADM_HEADER_LEN;
    return buf;
}

unsigned char *fsap_encode_rsp_start_kb(int retval,
                                        const unsigned char *kb_name,
                                        const unsigned char *msg,
                                        int *len)
{
    return encode_kb_response(ADM_RSP_START_KB, retval, kb_name, msg, len);
}

fsa_kb_response *fsap_decode_rsp_start_kb(const unsigned char *buf)
{
    return decode_kb_response(buf);
}

unsigned char *fsap_encode_cmd_get_kb_info(const unsigned char *kb_name,
                                           int *len)
{
    int data_len = strlen((char *)kb_name);
    unsigned char *buf = init_packet(ADM_CMD_GET_KB_INFO, data_len);
    unsigned char *p = buf;
    p += ADM_HEADER_LEN;

    memcpy(p, kb_name, data_len);
    *len = data_len + ADM_HEADER_LEN;
    return buf;
}

fsa_kb_info *fsap_decode_rsp_get_kb_info(const unsigned char *buf)
{
    return fsap_decode_rsp_get_kb_info_all(buf);
}

unsigned char *fsap_encode_cmd_get_kb_info_all(int *len)
{
    *len = ADM_HEADER_LEN;
    return fsap_encode_cmd_no_params(ADM_CMD_GET_KB_INFO_ALL);
}

fsa_kb_info *fsap_decode_rsp_get_kb_info_all(const unsigned char *buf)
{
    const unsigned char *p;
    uint8_t n_entries;
    uint8_t name_len;
    fsa_kb_info *first_ki = NULL;
    fsa_kb_info *ki;

    p = buf;

    memcpy(&n_entries, p, 1);
    p += 1;

    for (int i = 0; i < n_entries; i++) {
        /* create new kb_info obj, unpack fields into it */
        ki = fsa_kb_info_new();

        memcpy(&name_len, p, 1);
        p += 1;

        ki->name = (unsigned char *)malloc((name_len+1) * sizeof(char));
        memcpy(ki->name, p, name_len);
        ki->name[name_len] = '\0';
        p += name_len;

        memcpy(&(ki->pid), p, 4);
        p += 4;

        memcpy(&(ki->port), p, 2);
        p += 2;

        memcpy(&(ki->status), p, 1);
        p += 1;

        memcpy(&(ki->num_segments), p, 1);
        p += 1;

        memcpy(&(ki->p_segments_len), p, 1);
        p += 1;

        ki->p_segments_data = (uint8_t *)malloc(ki->p_segments_len);
        memcpy(ki->p_segments_data, p, ki->p_segments_len);
        p += ki->p_segments_len;

        ki->next = first_ki;
        first_ki = ki;
    }

    return first_ki;
}

/* send to indicate that a generic client/server error occured */
unsigned char *fsap_encode_rsp_error(const unsigned char *msg, int *len)
{
    int data_len = strlen((char *)msg);
    unsigned char *buf = init_packet(ADM_RSP_ERROR, data_len);
    unsigned char *p = buf;
    p += ADM_HEADER_LEN;

    memcpy(p, msg, data_len);
    *len = data_len + ADM_HEADER_LEN;
    return buf;
}

unsigned char *fsap_decode_rsp_error(const unsigned char *buf, int len)
{
    unsigned char *msg = (char *)malloc(len + 1);
    memcpy(msg, buf, len);
    msg[len] = '\0';

    return msg;
}

unsigned char *fsap_encode_rsp_get_kb_info(const fsa_kb_info *ki, int *len)
{
    uint8_t cmd;
    unsigned char *buf, *p;

    /* use same response format as kb_info_all... */
    buf = fsap_encode_rsp_get_kb_info_all(ki, len);

    /* ...but set command to different value */
    cmd = ADM_RSP_GET_KB_INFO;
    p = buf + ADM_H_LEN + ADM_H_VERS_LEN;
    memcpy(p, &cmd, ADM_H_CMD_LEN);

    return buf;
}

unsigned char *fsap_encode_rsp_get_kb_info_all(const fsa_kb_info *ki, int *len)
{
    int data_len = 1; /* number of kb entries, 0-255 */
    const fsa_kb_info *cur_ki;
    uint8_t n_entries = 0;
    unsigned char *buf, *p;

    uint8_t name_len;

    /* name_len name pid port status num_segments p_segments_len segments 
     * 1        *    4   2    1      1            1              *       */
    int base_entry_size = 10;
    
    /* loop through once to find size and number of entries */
    for (cur_ki = ki; cur_ki != NULL; cur_ki = cur_ki->next) {
        n_entries += 1;
        data_len += base_entry_size;
        data_len += strlen((char *)cur_ki->name);
        data_len += cur_ki->p_segments_len * sizeof(uint8_t);
    }

    /* create and fill buffer */
    buf = init_packet(ADM_RSP_GET_KB_INFO_ALL, data_len);
    p = buf;
    p += ADM_HEADER_LEN; /* move to start of data section */

    memcpy(p, &n_entries, 1);
    p += 1;

    for (cur_ki = ki; cur_ki != NULL; cur_ki = cur_ki->next) {
        name_len = (uint8_t)strlen((char *)cur_ki->name); 

        memcpy(p, &name_len, 1);
        p += 1;

        memcpy(p, cur_ki->name, name_len);
        p += name_len;

        memcpy(p, &(cur_ki->pid), 4);
        p += 4;

        memcpy(p, &(cur_ki->port), 2);
        p += 2;

        memcpy(p, &(cur_ki->status), 1);
        p += 1;

        memcpy(p, &(cur_ki->num_segments), 1);
        p += 1;

        memcpy(p, &(cur_ki->p_segments_len), 1);
        p += 1;

        memcpy(p, cur_ki->p_segments_data, cur_ki->p_segments_len);
        p += cur_ki->p_segments_len;
    }

    *len = data_len + ADM_HEADER_LEN;
    return buf;
}

unsigned char *fsap_encode_cmd_delete_kb(const unsigned char *kb_name,
                                         int *len)
{
    int data_len = strlen((char *)kb_name);
    unsigned char *buf = init_packet(ADM_CMD_DELETE_KB, data_len);
    unsigned char *p = buf;
    p += ADM_HEADER_LEN; /* move to start of data section */

    memcpy(p, kb_name, data_len);
    *len = data_len + ADM_HEADER_LEN;
    return buf;
}

unsigned char *fsap_encode_rsp_delete_kb(int retval,
                                         const unsigned char *kb_name,
                                         const unsigned char *msg,
                                         int *len)
{
    return encode_kb_response(ADM_RSP_DELETE_KB, retval, kb_name, msg, len);
}

fsa_kb_response *fsap_decode_rsp_delete_kb(const unsigned char *buf)
{
    return decode_kb_response(buf);
}
