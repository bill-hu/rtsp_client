#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include "rtspType.h"
#include "utils.h"
#include "rtsp.h"
#include "net.h"


inline static void RtspIncreaseCseq(RtspSession *sess);
inline static void RtspIncreaseCseq(RtspSession *sess)
{
    sess->cseq++;
    return;
}

int32_t RtspResponseStatus(char *response, char **error)
{
    int32_t size = 256, err_size = 0x00;
    int32_t offset = sizeof(RTSP_RESPONSE) - 1;
    char buf[8], *sep = NULL, *eol = NULL;
    *error = NULL;

    if (strncmp((const char*)response, (const char*)RTSP_RESPONSE, offset) != 0) {
        *error = calloc(1, size);
        snprintf((char *)*error, size, "Invalid RTSP response format");
        return -1;
    }

    sep = strchr((const char *)response+offset, ' ');
    if (!sep) {
        *error = calloc(1, size);
        snprintf((char *)*error, size, "Invalid RTSP response format");
        return -1;
    }

    memset(buf, '\0', sizeof(buf));
    strncpy((char *)buf, (const char *)(response+offset), sep-response-offset);

    eol = strchr(response, '\r');
    err_size = (eol - response) - offset - 1 - strlen(buf);
    *error = calloc(1, err_size + 1);
    if (NULL == *error){
        fprintf(stderr, "%s: Error calloc\n", __func__);
        return -1;
    }
    strncpy(*error, response + offset + 1 + strlen(buf), err_size);

    return atoi(buf);
}


int32_t RtspOptionsMsg(RtspSession *sess)
{
    int32_t num;
    int32_t ret = True;
    int32_t status;
    int32_t size = 4096;
    char *err = NULL;
    char buf[size];
    int32_t sock = sess->sockfd;

#ifdef RTSP_DEBUG
    printf("OPTIONS: command\n");
#endif

    memset(buf, '\0', sizeof(buf));
    num = snprintf(buf, size, CMD_OPTIONS, sess->ip, sess->port, sess->cseq);
    if (num < 0x00){
        fprintf(stderr, "%s : snprintf error!\n", __func__);
        return False;
    }
#ifdef RTSP_DEBUG
    DEBUG_REQ(buf);
#endif
    num = RtspTcpSendMsg(sock, buf, (uint32_t)num);
    if (num < 0){
        fprintf(stderr, "%s : Send Error\n", __func__);
        return False;
    }
#ifdef RTSP_DEBUG
    printf("OPTIONS: request sent\n");
#endif

    memset(buf, '\0', sizeof(buf));
    num = RtspTcpRcvMsg(sock, buf, size-1);
    if (num <= 0) {
        printf("Error: Server did not respond properly, closing...");
        return False;
    }

    status = RtspResponseStatus(buf, &err);
    if (status == 200){
        printf("OPTIONS: response status %i (%i bytes)\n", status, num);
    }
    else{
        printf("OPTIONS: response status %i: %s\n", status, err);
        ret = False;
    }
    if (NULL != err)
        free(err);
#ifdef RTSP_DEBUG
    DEBUG_RES(buf);
#endif
    RtspIncreaseCseq(sess);

    return ret;
}

static int32_t ParseSdpProto(char *buf, uint32_t size)
{
    char *p = strstr(buf, "\r\n\r\n");
    if (!p) {
        return -1;
    }

    /*[> Create buffer for DSP <]*/
    /*char *sdp = calloc(1, size+1);*/
    /*memset(sdp, '\0', size+1);*/
    /*strcpy(sdp, p + 4);*/

    /*[> sprop-parameter-sets key <]*/
    /*p = strstr(sdp, RTP_SPROP);*/
    /*if (!p) {*/
        /*return False;*/
    /*}*/

    /*char *end = strchr(p, '\r');*/
    /*if (!end) {*/
        /*return False;*/
    /*}*/

    /*int32_t prop_size = (end - p) - sizeof(RTP_SPROP) + 1;*/
    /**sprop = malloc(prop_size + 1);*/
    /*memcpy(*sprop, p + sizeof(RTP_SPROP) - 1, prop_size);*/

    return True;
}

int32_t RtspDescribeMsg(RtspSession *sess)
{
    int32_t num;
    int32_t ret = True;
    int32_t status;
    int32_t size = 4096;
    char *err;
    char buf[size];
    int32_t sock = sess->sockfd;

#ifdef RTSP_DEBUG
    RTSP_INFO("DESCRIBE: command\n");
#endif

    memset(buf, '\0', sizeof(buf));
    num = snprintf(buf, size, CMD_DESCRIBE, sess->url, sess->cseq);
    if (num < 0x00){
        fprintf(stderr, "%s : snprintf error!\n", __func__);
        return False;
    }

#ifdef RTSP_DEBUG
    DEBUG_REQ(buf);
#endif
    num = RtspTcpSendMsg(sock, buf, (uint32_t)num);
    if (num < 0){
        fprintf(stderr, "%s : Send Error\n", __func__);
        return False;
    }

#ifdef RTSP_DEBUG
    RTSP_INFO("DESCRIBE: request sent\n");
#endif

    memset(buf, '\0', sizeof(buf));
    num = RtspTcpRcvMsg(sock, buf, size-1);
    if (num <= 0) {
        printf("Error: Server did not respond properly, closing...");
        return False;
    }


    status = RtspResponseStatus(buf, &err);
    if (status == 200) {
        RTSP_INFO("DESCRIBE: response status %i (%i bytes)\n", status, num);
    }
    else {
        RTSP_INFO("DESCRIBE: response status %i: %s\n", status, err);
        ret = False;
    }
    if (NULL != err)
        free(err);

    ParseSdpProto(buf, num);
#ifdef RTSP_DEBUG
    DEBUG_RES("%s\n", buf);
#endif
    RtspIncreaseCseq(sess);

    return ret;
}

static int32_t ParseTransport(char *buf, uint32_t size, RtspSession *sess)
{
    char *p = strstr(buf, "Transport: ");
    if (!p) {
        RTSP_INFO("SETUP: Error, Transport header not found\n");
#ifdef RTSP_DEBUG
        DEBUG_RES(buf);
#endif
        return False;
    }

    char *sep = strchr(p, ';');
    if (NULL == sep)
        return False;
    return True;
}

static int32_t ParseSessionID(char *buf, uint32_t size, RtspSession *sess)
{
    /* Session ID */
    char *p = strstr(buf, SETUP_SESSION);
    if (!p) {
        RTSP_INFO("SETUP: Session header not found\n");
#ifdef RTSP_DEBUG
        DEBUG_RES(buf);
#endif
        return False;
    }
    char *sep = strchr((const char *)p, ';');
    if (NULL == sep){
        sep = strchr((const char *)p, '\r');
    }
    memset(sess->sessid, '\0', sizeof(sess->sessid));
    strncpy(sess->sessid, p+sizeof(SETUP_SESSION)-1, sep-p-sizeof(SETUP_SESSION)+1);
#ifndef RTSP_DEBUG
    printf("sessid : %s\n", sess->sessid);
#endif
    return True;
}

int32_t RtspSetupMsg(RtspSession *sess)
{
    int32_t num, ret = True, status;
    int32_t size = 4096;
    char *err;
    char buf[size];
    int32_t sock = sess->sockfd;

#ifndef RTSP_DEBUG
    RTSP_INFO("SETUP: command\n");
#endif

    memset(buf, '\0', sizeof(buf));
    num = snprintf(buf, size, CMD_SETUP, sess->url, sess->cseq);
    if (num < 0x00){
        fprintf(stderr, "%s : snprintf error!\n", __func__);
        return False;
    }
#ifdef RTSP_DEBUG
    DEBUG_REQ(buf);
#endif
    num = RtspTcpSendMsg(sock, buf, (uint32_t)num);
    if (num < 0){
        fprintf(stderr, "%s : Send Error\n", __func__);
        return False;
    }

#ifdef RTSP_DEBUG
    RTSP_INFO("SETUP: request sent\n");
#endif
    memset(buf, '\0', sizeof(buf));
    num = RtspTcpRcvMsg(sock, buf, size-1);
    if (num <= 0) {
        fprintf(stderr, "Error: Server did not respond properly, closing...");
        return False;
    }

    status = RtspResponseStatus(buf, &err);
    if (status == 200) {
        RTSP_INFO("SETUP: response status %i (%i bytes)\n", status, num);
        /*ParseTransport(buf, num, sess);*/
        ParseSessionID(buf, num, sess);
    }
    else {
        RTSP_INFO("SETUP: response status %i: %s\n", status, err);
#ifdef RTSP_DEBUG
        DEBUG_RES(buf);
#endif
        return False;
    }

    /* Fill session data */
    sess->packetization = 1; /* FIXME: project specific value */
#ifdef RTSP_DEBUG
    DEBUG_RES(buf);
#endif
    RtspIncreaseCseq(sess);
    return ret;
}

int32_t RtspPlayMsg(RtspSession *sess)
{
    int32_t num, ret = True, status, size=4096;
    char  *err, buf[size];
    int32_t sock = sess->sockfd;

#ifdef RTSP_DEBUG
    RTSP_INFO("PLAY: command\n");
#endif
    memset(buf, '\0', sizeof(buf));
    num = snprintf(buf, size, CMD_PLAY, sess->url, sess->cseq, sess->sessid);
    if (num < 0x00){
        fprintf(stderr, "%s : snprintf error!\n", __func__);
        return False;
    }
#ifdef RTSP_DEBUG
    DEBUG_REQ(buf);
#endif
    num = RtspTcpSendMsg(sock, buf, (uint32_t)num);
    if (num < 0){
        fprintf(stderr, "%s : Send Error\n", __func__);
        return False;
    }

#ifdef RTSP_DEBUG
    RTSP_INFO("PLAY: request sent\n");
#endif

    memset(buf, '\0', sizeof(buf));
    num = RtspTcpRcvMsg(sock, buf, size-1);
    if (num <= 0) {
        fprintf(stderr, "Error: Server did not respond properly, closing...");
        return False;
    }

    status = RtspResponseStatus(buf, &err);
    if (status == 200) {
        printf("PLAY: response status %i (%i bytes)\n", status, num);
    }
    else {
        fprintf(stderr, "PLAY: response status %i: %s\n", status, err);
        ret = False;
    }

#ifdef RTSP_DEBUG
    DEBUG_RES(buf);
#endif
    RtspIncreaseCseq(sess);
    return ret;
}
