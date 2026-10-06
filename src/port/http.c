/* HTTP downloads, see http.h (adapted from the DOOM port's). */
#include "http.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __PROSPERO__

#include <stddef.h>
#include <stdint.h>

#include "url.h"

int sceNetPoolCreate(const char *name, int size, int flags);
int sceSslInit(size_t pool_size);
int sceHttpInit(int net_pool, int ssl_context, size_t pool_size);
int sceHttpCreateTemplate(int context, const char *user_agent, int version, int auto_proxy);
int sceHttpSetAutoRedirect(int id, int enabled);
int sceHttpSetResolveTimeOut(int id, uint32_t usec);
int sceHttpSetConnectTimeOut(int id, uint32_t usec);
int sceHttpSetSendTimeOut(int id, uint32_t usec);
int sceHttpSetRecvTimeOut(int id, uint32_t usec);
int sceHttpsEnableOption(int id, uint32_t flags);
int sceHttpCreateConnectionWithURL(int template_id, const char *url, int keep_alive);
int sceHttpCreateRequestWithURL(int connection, int method, const char *url, uint64_t content_length);
int sceHttpAddRequestHeader(int id, const char *name, const char *value, uint32_t mode);
int sceHttpSendRequest(int request, const void *data, size_t size);
int sceHttpGetStatusCode(int request, int *status);
int sceHttpGetResponseContentLength(int request, int *result, uint64_t *length);
int sceHttpGetAllResponseHeaders(int request, char **headers, size_t *size);
int sceHttpParseResponseHeader(const char *headers, size_t size, const char *name, const char **value,
                               size_t *value_size);
int sceHttpReadData(int request, void *data, size_t size);
int sceHttpDeleteRequest(int request);
int sceHttpDeleteConnection(int connection);

#define NET_POOL (1024 * 1024)
#define SSL_POOL (304 * 1024)
#define HTTP_POOL (4 * 1024 * 1024)
#define HTTP_1_1 2
#define METHOD_GET 0
#define HEADER_OVERWRITE 0
#define TIMEOUT_US 15000000
#define MAX_REDIRECTS 8
/* Check the server's certificate: its chain, its name and its dates. */
#define VERIFY_FLAGS (0x01 | 0x04 | 0x08 | 0x10 | 0x20 | 0x80)

struct http
{
    int connection, request;
};

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int template_id = -1;

static int start_http(void)
{
    int pool, ssl, http, result;

    pool = sceNetPoolCreate("ps5-native-ra", NET_POOL, 0);
    if (pool < 0)
        return pool;
    ssl = sceSslInit(SSL_POOL);
    if (ssl < 0)
        return ssl;
    http = sceHttpInit(pool, ssl, HTTP_POOL);
    if (http < 0)
        return http;
    result = sceHttpCreateTemplate(http, "PS5-Native-RA", HTTP_1_1, 0);
    if (result < 0)
        return result;
    template_id = result;
    if ((result = sceHttpSetAutoRedirect(template_id, 0)) < 0 ||
        (result = sceHttpSetResolveTimeOut(template_id, TIMEOUT_US)) < 0 ||
        (result = sceHttpSetConnectTimeOut(template_id, TIMEOUT_US)) < 0 ||
        (result = sceHttpSetSendTimeOut(template_id, TIMEOUT_US)) < 0 ||
        (result = sceHttpSetRecvTimeOut(template_id, TIMEOUT_US)) < 0 ||
        (result = sceHttpsEnableOption(template_id, VERIFY_FLAGS)) < 0)
        return result;
    return 0;
}

static void header_value(int request, const char *name, char *out, size_t size)
{
    char *headers = NULL;
    const char *value = NULL;
    size_t headers_size = 0, value_size = 0;

    out[0] = '\0';
    if (sceHttpGetAllResponseHeaders(request, &headers, &headers_size) < 0 ||
        sceHttpParseResponseHeader(headers, headers_size, name, &value, &value_size) < 0)
        return;
    if (value_size >= size)
        value_size = size - 1;
    memcpy(out, value, value_size);
    out[value_size] = '\0';
}

static int send_get(http_t *http, const char *url, long long offset, int *status)
{
    char range[40];
    int result;

    http->connection = result = sceHttpCreateConnectionWithURL(template_id, url, 1);
    if (result >= 0)
        http->request = result = sceHttpCreateRequestWithURL(http->connection, METHOD_GET, url, 0);
    if (result >= 0 && offset > 0)
    {
        snprintf(range, sizeof(range), "bytes=%lld-", offset);
        result = sceHttpAddRequestHeader(http->request, "Range", range, HEADER_OVERWRITE);
    }
    if (result >= 0)
        result = sceHttpSendRequest(http->request, NULL, 0);
    if (result >= 0)
        result = sceHttpGetStatusCode(http->request, status);
    return result;
}

static void drop_request(http_t *http)
{
    if (http->request >= 0)
        sceHttpDeleteRequest(http->request);
    if (http->connection >= 0)
        sceHttpDeleteConnection(http->connection);
    http->request = http->connection = -1;
}

static int is_redirect(int status)
{
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

http_t *http_get(const char *url, long long offset, http_info_t *info, char *error, int error_size)
{
    char location[sizeof(info->url)], next[sizeof(info->url)];
    uint64_t length = 0;
    int status = 0, has_length = -1, redirects, result;

    pthread_mutex_lock(&lock);
    result = template_id < 0 ? start_http() : 0;
    pthread_mutex_unlock(&lock);
    if (result < 0)
    {
        snprintf(error, (size_t)error_size, "the network could not be set up (0x%08x)", (unsigned)result);
        return NULL;
    }

    http_t *http = calloc(1, sizeof(*http));
    if (http == NULL)
        return NULL;
    http->request = http->connection = -1;

    /* Redirects are followed here rather than by the library, to learn the address the request
     * ends up at: a folder listing's relative links are relative to that one. */
    snprintf(info->url, sizeof(info->url), "%s", url);
    for (redirects = 0;; redirects++)
    {
        result = send_get(http, info->url, offset, &status);
        if (result < 0 || !is_redirect(status) || redirects == MAX_REDIRECTS)
            break;
        header_value(http->request, "Location", location, sizeof(location));
        if (location[0] == '\0')
            break;
        url_resolve(next, sizeof(next), info->url, location);
        snprintf(info->url, sizeof(info->url), "%s", next);
        drop_request(http);
    }
    if (result < 0)
    {
        snprintf(error, (size_t)error_size, "the connection failed (0x%08x)", (unsigned)result);
        http_close(http);
        return NULL;
    }
    if (status != 200 && status != 206)
    {
        snprintf(error, (size_t)error_size, "the server answered %d", status);
        http_close(http);
        return NULL;
    }
    sceHttpGetResponseContentLength(http->request, &has_length, &length);
    info->status = status;
    info->length = has_length == 0 ? (long long)length : -1;
    header_value(http->request, "Content-Type", info->type, sizeof(info->type));
    char ranges[32];
    header_value(http->request, "Accept-Ranges", ranges, sizeof(ranges));
    info->ranges = strstr(ranges, "bytes") != NULL;
    return http;
}

int http_read(http_t *http, void *buffer, int size)
{
    int got = sceHttpReadData(http->request, buffer, (size_t)size);
    if (got < 0)
        printf("http: reading failed (0x%08x)\n", (unsigned)got);
    return got < 0 ? -1 : got;
}

void http_close(http_t *http)
{
    if (http == NULL)
        return;
    drop_request(http);
    free(http);
}

#else /* libcurl, for the tests on a PC */

#include <curl/curl.h>

#define BUFFER_LIMIT (1 << 20)

struct http
{
    CURL *easy;
    CURLM *multi;
    char *data;
    size_t used, read_at;
    int paused, finished;
    CURLcode result;
};

static pthread_once_t once = PTHREAD_ONCE_INIT;

static void start_curl(void)
{
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

static size_t receive(char *chunk, size_t size, size_t count, void *user)
{
    http_t *http = user;
    size_t bytes = size * count;

    if (http->used - http->read_at >= BUFFER_LIMIT)
    {
        http->paused = 1;
        return CURL_WRITEFUNC_PAUSE;
    }
    if (http->read_at > 0)
    {
        memmove(http->data, http->data + http->read_at, http->used - http->read_at);
        http->used -= http->read_at;
        http->read_at = 0;
    }
    char *grown = realloc(http->data, http->used + bytes);
    if (grown == NULL)
        return 0;
    http->data = grown;
    memcpy(http->data + http->used, chunk, bytes);
    http->used += bytes;
    return bytes;
}

static void pump(http_t *http)
{
    CURLMsg *message;
    int running, left;

    if (http->paused)
    {
        http->paused = 0;
        curl_easy_pause(http->easy, CURLPAUSE_CONT);
    }
    curl_multi_perform(http->multi, &running);
    while ((message = curl_multi_info_read(http->multi, &left)) != NULL)
    {
        if (message->msg == CURLMSG_DONE)
        {
            http->finished = 1;
            http->result = message->data.result;
        }
    }
    if (!http->finished && http->used == http->read_at)
        curl_multi_poll(http->multi, NULL, 0, 100, NULL);
}

http_t *http_get(const char *url, long long offset, http_info_t *info, char *error, int error_size)
{
    char range[32];
    long status = 0;
    curl_off_t length = -1;
    const char *type = NULL, *final = NULL;

    pthread_once(&once, start_curl);
    http_t *http = calloc(1, sizeof(*http));
    if (http == NULL)
        return NULL;
    http->easy = curl_easy_init();
    http->multi = curl_multi_init();
    curl_easy_setopt(http->easy, CURLOPT_URL, url);
    curl_easy_setopt(http->easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(http->easy, CURLOPT_MAXREDIRS, 8L);
    curl_easy_setopt(http->easy, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(http->easy, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(http->easy, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(http->easy, CURLOPT_USERAGENT, "PS5-Native-RA");
    curl_easy_setopt(http->easy, CURLOPT_WRITEFUNCTION, receive);
    curl_easy_setopt(http->easy, CURLOPT_WRITEDATA, http);
    if (offset > 0)
    {
        snprintf(range, sizeof(range), "%lld-", offset);
        curl_easy_setopt(http->easy, CURLOPT_RANGE, range);
    }
    curl_multi_add_handle(http->multi, http->easy);

    while (!http->finished && http->used == 0)
        pump(http);
    curl_easy_getinfo(http->easy, CURLINFO_RESPONSE_CODE, &status);
    if (http->finished && http->result != CURLE_OK)
    {
        snprintf(error, (size_t)error_size, "%s", curl_easy_strerror(http->result));
        http_close(http);
        return NULL;
    }
    if (status != 200 && status != 206)
    {
        snprintf(error, (size_t)error_size, "the server answered %ld", status);
        http_close(http);
        return NULL;
    }
    curl_easy_getinfo(http->easy, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &length);
    curl_easy_getinfo(http->easy, CURLINFO_CONTENT_TYPE, &type);
    curl_easy_getinfo(http->easy, CURLINFO_EFFECTIVE_URL, &final);
    struct curl_header *ranges = NULL;
    info->status = (int)status;
    info->length = length;
    snprintf(info->type, sizeof(info->type), "%s", type != NULL ? type : "");
    snprintf(info->url, sizeof(info->url), "%s", final != NULL ? final : url);
    info->ranges = curl_easy_header(http->easy, "Accept-Ranges", 0, CURLH_HEADER, -1, &ranges) == CURLHE_OK &&
                   strstr(ranges->value, "bytes") != NULL;
    return http;
}

int http_read(http_t *http, void *buffer, int size)
{
    while (http->used == http->read_at && !http->finished)
        pump(http);
    size_t available = http->used - http->read_at;
    if (available == 0)
        return http->result == CURLE_OK ? 0 : -1;
    if (available > (size_t)size)
        available = (size_t)size;
    memcpy(buffer, http->data + http->read_at, available);
    http->read_at += available;
    return (int)available;
}

void http_close(http_t *http)
{
    if (http == NULL)
        return;
    curl_multi_remove_handle(http->multi, http->easy);
    curl_easy_cleanup(http->easy);
    curl_multi_cleanup(http->multi);
    free(http->data);
    free(http);
}

#endif
