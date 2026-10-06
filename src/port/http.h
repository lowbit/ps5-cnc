/* HTTP(S) downloads for the importer: links the player types, the folder listings they lead to,
 * and the free Red Alert discs. On the PS5 through the system's HTTP library (with its TLS), on a
 * PC (for the tests) through libcurl. */
#ifndef PS5_HTTP_H
#define PS5_HTTP_H

typedef struct
{
    int status;       /* 200, or 206 for a request from an offset */
    long long length; /* of the body, -1 when the server does not say */
    char type[64];    /* its Content-Type */
    int ranges;       /* the server says it answers requests from an offset (Accept-Ranges) */
    char url[768];    /* where the request ended up after redirects */
} http_info_t;

typedef struct http http_t;

/* Starts a GET of url from offset (a Range request when it is not 0). NULL on failure, with the
 * reason in error. */
http_t *http_get(const char *url, long long offset, http_info_t *info, char *error, int error_size);

/* Reads up to size bytes of the body: how many, 0 at its end, -1 on failure. */
int http_read(http_t *http, void *buffer, int size);

void http_close(http_t *http);

#endif
