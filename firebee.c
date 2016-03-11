#include <sys/types.h>
#include <sys/stat.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <getopt.h>
#include <event.h>
#include <evhttp.h>
#include <hiredis/hiredis.h>

struct context
{
    int instance_id;
    long sequence;
    long long last_timestamp;
    long long twepoch;

    unsigned char instance_id_bits;
    unsigned char sequence_bits;

    int instance_id_shift;
    int timestamp_left_shift;

    int sequence_mask;
};

static struct context g_ctx;


#define fatal(fmt, ...) do {             \
    fprintf(stderr, fmt, ##__VA_ARGS__); \
    exit(1);                             \
} while (0)


void daemonize(void)
{
    int fd;

    if (fork() != 0) exit(0);

    setsid();

    if ((fd = open("/dev/null", O_RDWR, 0)) != -1) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > STDERR_FILENO)
            close(fd);
    }
}

void context_init()
{
    g_ctx.instance_id          = 0;
    g_ctx.sequence             = 0;
    g_ctx.last_timestamp       = -1LL;
    g_ctx.twepoch              = 1288834974657LL;
    g_ctx.instance_id_bits     = 10;
    g_ctx.sequence_bits        = 12;
    g_ctx.instance_id_shift    = g_ctx.sequence_bits;
    g_ctx.timestamp_left_shift = g_ctx.sequence_bits + g_ctx.instance_id_bits;
    g_ctx.sequence_mask        = -1 ^ (-1 << g_ctx.sequence_bits);
}

static long long realtime()
{
    struct timeval tv;
    long long retval;

    if (gettimeofday(&tv, NULL) == -1) {
        return 0LL;
    }

    retval = (long long)tv.tv_sec * 1000ULL
           + (long long)tv.tv_usec / 1000ULL;

    return retval;
}

long long nextid(struct context *ctx)
{
    long long ts = realtime();

    if (ts == 0LL) {
        return -1LL;
    }

    if (ctx->last_timestamp == ts) {
        ctx->sequence = (ctx->sequence + 1) & ctx->sequence_mask;
        if (ctx->sequence == 0) {
            while (ctx->last_timestamp == (ts = realtime()));
        }

    } else {
        ctx->sequence = 0;
    }

    ctx->last_timestamp = ts;

    return ((ts - ctx->twepoch) << ctx->timestamp_left_shift)
            | (ctx->instance_id << ctx->instance_id_shift)
            | ctx->sequence;
}

void request_handler(struct evhttp_request *req, void *arg)
{
    struct evbuffer *buf = evbuffer_new();

    evbuffer_add_printf(buf, "%lld", nextid(&g_ctx));
    evhttp_send_reply(req, HTTP_OK, "OK", buf);

    evbuffer_free(buf);

    return;
}

void usage()
{
    printf("Usage: firebee [options]\n"
           "Options:\n"
           "    -r <host>        redis host and port\n"
           "    -l <addr>        IP/host listen to\n"
           "    -p <port>        port number\n"
           "    -d               run at daemon mode\n");
    exit(0);
}

static const struct option options[] = {
    {"redis",  2, NULL, 'r'},
    {"listen", 2, NULL, 'l'},
    {"port",   2, NULL, 'p'},
    {"daemon", 0, NULL, 'd'},
    {"help",   0, NULL, 'h'},
};

static char *l_host = "127.0.0.1";
static short l_port = 8080;
static int daemon_mode = 0;
static char *redis_addr = NULL;
static short redis_port = 6379;

int parse_redis(char *host)
{
    struct hostent *he;
    struct in_addr **addr_list;
    char *port;
    int i;

    if ((port = strchr(host, ':'))) {
        *port++ = '\0';
    }

    he = gethostbyname(host);
    if (!he) {
        return -1;
    }

    addr_list = (struct in_addr **)he->h_addr_list;

    for(i = 0; addr_list[i] != NULL; i++) {
        redis_addr = inet_ntoa(*addr_list[i]);
        break;
    }

    if (port) {
        redis_port = atoi(port);
    }

    return 0;
}

void set_instance_id()
{
    static redisContext *redis;
    redisReply *reply = NULL;
    int retval = -1;

    if (!redis_addr) {
        fatal("Fatal: Redis host have not set, using `-r' option to set\n");
    }

    redis = redisConnect(redis_addr, redis_port);
    if (redis == NULL || redis->err) {
        fatal("Fatal: Can not connect to Redis server\n");
    }

    reply = redisCommand(redis, "INCR *firebee_instance_id*");

    if (redis->err == 0
        && reply != NULL
        && reply->type == REDIS_REPLY_INTEGER)
    {
        g_ctx.instance_id = (int)reply->integer;

    } else {
    	fatal("Fatal: Can not get instance ID from Redis server\n");
    }

    freeReplyObject(reply);
    redisFree(redis);
}

void parse_options(int argc, char **argv)
{
    int opt, i;

    while ((opt = getopt_long(argc, argv,
                                 "r:l:p:dh", options, &i)) != -1)
    {
        switch (opt) {
        case 'r':
            if (parse_redis(optarg)) {
                fatal("Fatal: Can not get the Redis IP address\n");
            }
            break;
        case 'l':
            l_host = strdup(optarg);
            break;
        case 'p':
            l_port = atoi(optarg);
            break;
        case 'd':
            daemon_mode = 1;
            break;
        case 'h':
        case '?':
            usage();
        }
    }
}

int main(int argc, char **argv)
{
    struct evhttp *httpd;

    context_init();
    parse_options(argc, argv);
    set_instance_id();

    if (daemon_mode) {
        daemonize();
    }

    event_init();

    httpd = evhttp_start(l_host, l_port);

    evhttp_set_cb(httpd, "/gen", request_handler, NULL);
    event_dispatch();

    return 0;
}
