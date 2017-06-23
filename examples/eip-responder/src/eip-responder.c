#define LOG_TAG "eip-responder"

#include <dslink/log.h>
#include <dslink/ws.h>
#include "eip-responder.h"

void 
invoke_new_connection(
    DSLink *link, 
    DSNode *node,
    json_t *rid, 
    json_t *params, 
    ref_t *stream_ref) 
{
    (void) node;    
    (void) stream_ref;

    //
    //  params 에서 ip, host 정보를 추출하고, child 노드를 생성한다.
    // 
    const char* ip = json_string_value(json_object_get(params, "ip"));
    const char* host = json_string_value(json_object_get(params, "host"));
    if (NULL == ip || NULL== host)
    {
        // 
        //  todo , 에러 메세지 응답하기 
        // 
        log_warn("Invalid params. \n");
        return;
    }

    //
    //  /downstream/EIP-Responder/connections/ 아래에 child node 를 생성한다.
    //  동일한 이름의 호스트가 존재하면 생성하지 않는다. 
    // 
    DSNode* parent = node->parent;
    if (!(parent->children && dslink_map_contains(parent->children, (char*)host))) 
    {
        DSNode* new_host = dslink_node_create(parent, host, "node");
        if (NULL == new_host)
        {
            log_warn("Failed to create the connections node\n");
            return;
        }

        if (dslink_node_add_child(link, new_host) != 0) 
        {
            log_warn("Failed to add the new_host node to the connections\n");
            dslink_node_tree_free(link, new_host);
            return;
        }
    }


    // 
    //  노드 메타데이터 설정 등등...
    // 


    //
    //  응답 데이터 전송
    // 
    /*

            {
                "rid": 1,
                "stream": "closed",
                "columns": [
                    {"name": "result", "type": "string", "meta": { "test": true, "unit": "F" } }
                ],
                "updates": [
                    ["dsaisawesome"]
                ]
            }


                            
            {
                "rid": 7,
                "columns": [
                    {
                    "name": "ip",
                    "type": "string"
                    },
                    {
                    "name": "host",
                    "type": "string"
                    }
                ],
                "stream": "closed",
                "updates": [
                    [
                    "ip",
                    "host"
                    ]
                ]
            }

    */
    json_t *top = json_object();
    if (!top) {
        return;
    }
    json_t *resps = json_array();
    if (!resps) {
        json_delete(top);
        return;
    }
    json_object_set_new_nocheck(top, "responses", resps);

    json_t *resp = json_object();
    if (!resp) {
        json_delete(top);
        return;
    }
    json_t *updates = json_array();
    

    //
    //  ip/host
    //  todo 응답 데이터 구성이 잘못된것 같은데 테스트 필요
    // 
    json_t* update = json_array();
    json_array_append_new(updates, update);
    json_array_append_new(update, json_string("ip"));
    json_array_append_new(update, json_string("host"));


    json_object_set_new_nocheck(resp, "updates", updates);

    json_array_append_new(resps, resp);

    json_object_set_new_nocheck(resp, "stream", json_string("closed"));
    json_object_set_nocheck(resp, "rid", rid);

    #ifdef MYDBG
    {
        log_info("params = \nip=%s, host=%s \n",ip, host);

        char* data = json_dumps(resp, JSON_INDENT(2));
        log_info ("resps = \n%s \n", data);
        dslink_free(data);
    }
    #endif//MYDBG

    dslink_ws_send_obj((struct wslay_event_context *) link->_ws, top);
    json_delete(top);
}





/// @brief  
void eip_responder_init(DSLink *link, DSNode *root) {
    
    //
    // todo. 
    //      코드 중간에 에러가 발생한 경우 리소스 릭이 발생할 수 있다. 
    //      꼼꼼히 따져보자. 
    // 

    DSNode *connections = dslink_node_create(root, "connections", "node");
    if (!connections) {
        log_warn("Failed to create the connections node\n");
        return;
    }

    // connections->on_list_open = list_opened;
    // connections->on_list_close = list_closed;

    if (dslink_node_add_child(link, connections) != 0) {
        log_warn("Failed to add the connections node to the root\n");
        dslink_node_tree_free(link, connections);
        return;
    }

    //
    //  `/connections/new` action node 를 생성하고, invoke 핸들러를 등록한다. 
    // 
    DSNode *new_connection = dslink_node_create(connections, "new", "node");
    if (!new_connection) {
        log_warn("Failed to create new_connection action node\n");
        return;
    }

    new_connection->on_invocation = invoke_new_connection;
    dslink_node_set_meta(link, new_connection, "$name", json_string("Add New Host"));
    dslink_node_set_meta(link, new_connection, "$invokable", json_string("read"));

    //
    //  action 의 response stream 구조를 정의한다. 
    //  
    json_t *columns = json_array();
    json_t *column_ip = json_object();
    json_object_set_new(column_ip, "name", json_string("ip"));
    json_object_set_new(column_ip, "type", json_string("string"));
    json_array_append_new(columns, column_ip);

    json_t *column_host = json_object();
    json_object_set_new(column_host, "name", json_string("host"));
    json_object_set_new(column_host, "type", json_string("string"));    
    json_array_append_new(columns, column_host);

    // #ifdef MYDBG
    // {
    //     char* data = json_dumps(columns, JSON_INDENT(2));
    //     log_info("columns = %s \n", data);
    //     dslink_free(data);
    // }
    // #endif//MYDBG


    //
    //  invoke 파라미터를 정의한다. 
    // 
    json_t *params = json_array();
    json_t *param_ip = json_object();

    /// IP
    json_object_set_new(param_ip, "name", json_string("ip"));
    json_object_set_new(param_ip, "type", json_string("string"));            
    json_array_append_new(params, param_ip);

    /// Host name
    json_t *param_host = json_object();
    json_object_set_new(param_host, "name", json_string("host"));
    json_object_set_new(param_host, "type", json_string("string"));    
    json_array_append_new(params, param_host);


    // #ifdef MYDBG
    // {
    //     char* data = json_dumps(params, JSON_INDENT(2));
    //     log_info("params = %s \n", data);
    //     dslink_free(data);
    // }
    // #endif//MYDBG


    dslink_node_set_meta(link, new_connection, "$columns", columns);
    dslink_node_set_meta(link, new_connection, "$params", params);

    if (dslink_node_add_child(link, new_connection) != 0) {
        log_warn("Failed to add new_connection action to the eip-responder node\n");
        dslink_node_tree_free(link, new_connection);
        return;
    }
}
