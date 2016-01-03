#define LOG_TAG "dslink"

#include <argtable3.h>
#include <string.h>
#include "dslink/handshake.h"
#include "dslink/log.h"
#include "dslink/utils.h"
#include "dslink/ws.h"

#define DSLINK_RESPONDER_MAP_INIT(var, type) \
    responder->var = calloc(1, sizeof(Map)); \
    if (!responder->var) { \
        goto cleanup; \
    } \
    if (dslink_map_init(responder->var, \
    dslink_map_##type##_cmp, \
    dslink_map_##type##_key_len_cal) != 0) { \
        free(responder->var); \
        responder->var = NULL; \
        goto cleanup; \
    }

static inline
void dslink_print_help() {
    printf("See --help for usage\n");
}

static
int dslink_parse_opts(int argc,
                      char **argv,
                      DSLinkConfig *config) {
    int ret = 0;
    struct arg_lit *help;
    struct arg_str *broker, *log;
    struct arg_end *end;

    void *argTable[] = {
        help = arg_lit0("h", "help", "Displays this help menu"),
        broker = arg_str1("b", "broker", "url", "Sets the broker URL to connect to"),
        log = arg_str0("l", "log", "log type", "Sets the logging level"),
        end = arg_end(5)
    };

    if (arg_nullcheck(argTable) != 0) {
        return DSLINK_ALLOC_ERR;
    }

    int errs = arg_parse(argc, argv, argTable);

    if (help->count > 0) {
        printf("Usage: <opts>\n");
        arg_print_glossary(stdout, argTable, " %-25s %s\n");
        ret = 1;
        goto exit;
    }

    if (errs > 0) {
        dslink_print_help();
        arg_print_errors(stdout, end, ":");
        ret = 1;
        goto exit;
    }

    config->broker_url = broker->sval[0];

    if (log->count > 0) {
        char lvl[8];
        const char *src = log->sval[0];
        size_t len = strlen(src);
        if (len > sizeof(lvl)) {
            len = sizeof(lvl);
        }
        memcpy(lvl, src, len);
        if (dslink_log_set_lvl(lvl, len) != 0) {
            printf("Invalid log level: %s\n", lvl);
            dslink_print_help();
            ret = 1;
            goto exit;
        }
    }

exit:
    arg_freetable(argTable, sizeof(argTable) / sizeof(argTable[0]));
    return ret;
}

static
int dslink_init_responder(Responder *responder) {
    responder->super_root = dslink_node_create(NULL, "/", "node");
    if (!responder->super_root) {
        goto cleanup;
    }

    DSLINK_RESPONDER_MAP_INIT(open_streams, uint32)
    DSLINK_RESPONDER_MAP_INIT(list_subs, str)
    DSLINK_RESPONDER_MAP_INIT(value_path_subs, str)
    DSLINK_RESPONDER_MAP_INIT(value_sid_subs, uint32)
    return 0;
cleanup:
    if (responder->open_streams) {
        DSLINK_MAP_FREE(responder->open_streams, {});
    }
    if (responder->list_subs) {
        DSLINK_MAP_FREE(responder->list_subs, {});
    }
    if (responder->value_path_subs) {
        DSLINK_MAP_FREE(responder->value_path_subs, {});
    }
    if (responder->value_sid_subs) {
        DSLINK_MAP_FREE(responder->value_sid_subs, {});
    }
    DSLINK_CHECKED_EXEC(dslink_node_tree_free, responder->super_root);
    return DSLINK_ALLOC_ERR;
}

int dslink_init(int argc, char **argv,
                const char *name, uint8_t isRequester,
                uint8_t isResponder, DSLinkCallbacks *cbs) {
    mbedtls_ecdh_context ctx;
    DSLinkConfig config;

    Url *url = NULL;
    json_t *handshake = NULL;
    char *dsId = NULL;
    Socket *sock = NULL;

    DSLink link;
    memset(&link, 0, sizeof(DSLink));

    int ret = 0;
    config.name = name;
    if ((ret = dslink_parse_opts(argc, argv, &config)) != 0) {
        if (ret == DSLINK_ALLOC_ERR) {
            log_fatal("Failed to allocate memory during argument parsing\n");
        }
        return 1;
    }

    // TODO: move .key as a parameter
    if ((ret = dslink_handshake_key_pair_fs(&ctx, ".key")) != 0) {
        if (ret == DSLINK_CRYPT_KEY_DECODE_ERR) {
            log_fatal("Failed to decode existing key\n");
        } else if (ret == DSLINK_OPEN_FILE_ERR) {
            log_fatal("Failed to write generated key to disk\n");
        } else if (ret == DSLINK_CRYPT_KEY_PAIR_GEN_ERR) {
            log_fatal("Failed to generated key\n");
        } else {
            log_fatal("Unknown error occurred during key handling: %d\n", ret);
        }
        ret = 1;
        goto exit;
    }

    url = dslink_url_parse(config.broker_url);
    if (!url) {
        log_fatal("Failed to parse url: %s\n", config.broker_url);
        ret = 1;
        goto exit;
    }

    if (isResponder) {
        link.responder = calloc(1, sizeof(Responder));
        if (!link.responder) {
            log_fatal("Failed to create responder\n");
            goto exit;
        }

        if (dslink_init_responder(link.responder) != 0) {
            log_fatal("Failed to initialize responder\n");
            goto exit;
        }
    }

    if (cbs->init_cb) {
        cbs->init_cb(&link);
    }

    if ((ret = dslink_handshake_generate(url, &ctx, config.name,
                                         isRequester, isResponder,
                                         &handshake, &dsId)) != 0) {
        log_fatal("Handshake failed: %d\n", ret);
        ret = 1;
        goto exit;
    }

    const char *uri = json_string_value(json_object_get(handshake, "wsUri"));
    const char *tKey = json_string_value(json_object_get(handshake, "tempKey"));
    const char *salt = json_string_value(json_object_get(handshake, "salt"));

    if (!(uri && tKey && salt)) {
        log_fatal("Handshake didn't return the "
                      "necessary parameters to complete\n");
        ret = 1;
        goto exit;
    }

    if ((ret = dslink_handshake_connect_ws(url, &ctx, uri,
                                           tKey, salt, dsId, &sock)) != 0) {
        log_fatal("Failed to connect to the broker: %d\n", ret);
        ret = 1;
        goto exit;
    } else {
        log_info("Successfully connected to the broker\n");
    }

    if (cbs->on_connected_cb) {
        cbs->on_connected_cb(&link);
    }

    link._socket = sock;
    dslink_handshake_handle_ws(&link);

    // TODO: automatic reconnecting
    log_warn("Disconnected from the broker\n")
    if (cbs->on_disconnected_cb) {
        cbs->on_disconnected_cb(&link);
    }

exit:
    mbedtls_ecdh_free(&ctx);
    DSLINK_CHECKED_EXEC(dslink_socket_close, sock);
    if (link.responder) {
        DSLINK_CHECKED_EXEC(dslink_node_tree_free, link.responder->super_root);
        if (link.responder->open_streams) {
            DSLINK_MAP_FREE(link.responder->open_streams, {
                free(entry->key);
                free(entry->value);
            });
            free(link.responder->open_streams);
        }

        if (link.responder->value_path_subs) {
            DSLINK_MAP_FREE(link.responder->value_path_subs, {
                free(entry->value);
            });
            free(link.responder->value_path_subs);
        }

        if (link.responder->value_sid_subs) {
            DSLINK_MAP_FREE(link.responder->value_sid_subs, {});
            free(link.responder->value_sid_subs);
        }

        if (link.responder->list_subs) {
            DSLINK_MAP_FREE(link.responder->list_subs, {
                free(entry->value);
            });
            free(link.responder->list_subs);
        }

        free(link.responder);
    }
    DSLINK_CHECKED_EXEC(free, dsId);
    DSLINK_CHECKED_EXEC(dslink_url_free, url);
    DSLINK_CHECKED_EXEC(json_delete, handshake);
    return ret;
}