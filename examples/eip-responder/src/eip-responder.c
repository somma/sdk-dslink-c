#define LOG_TAG "eip-responder"
#include <string.h>
#include <stdio.h>
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
    const char* host = json_string_value(json_object_get(params, "port"));
    if (NULL == ip || NULL== host)
    {
        log_warn("Invalid params. \n");
        return;
    }

    //
    //  /downstream/EIP-Responder/connections/ 아래에 child node 를 생성한다.
    //  현재 노드는 /EIP-Responder/new 이다. 
    //  동일한 이름의 호스트가 존재하면 생성하지 않는다. 
    // 
    char host_name[32] = {0};
    sprintf(host_name, "host_%s", ip);


    DSNode* root = node->parent;
    ref_t* ref = dslink_map_get(root->children, "connections");
    if (NULL == ref)
    {
        log_warn("No connections node.");
        return;
    }
    
    DSNode* connections = ref->data;    
    {
        if (!(connections->children && dslink_map_contains(connections->children, (char*)ip))) 
        {
            DSNode* new_host = dslink_node_create(connections, host_name, "node");
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
    }

    //
    //  응답 데이터 전송
    //  https://github.com/IOT-DSA/docs/wiki/Node-API#responses 참고
    //
    // {
    //     "msg": 18,
    //     "responses": [
    //         {
    //             "rid": 2,
    //             "stream": "open",
    //             "columns": [
    //                 {
    //                     "name": "ip",
    //                     "type": "string"
    //                 },
    //                 {
    //                     "name": "host",
    //                     "type": "string"
    //                 }
    //             ],
    //             "updates": [
    //                 [
    //                     "1.1.1.1",
    //                     "host01"
    //                 ]
    //             ]
    //         }
    //     ]
    // }
    json_t *top = json_object();
    if (!top) {
        return;
    }

    json_t *responses = json_array();
    if (!responses) { json_delete(top); return; }
    json_object_set_new_nocheck(top, "responses", responses);

    json_t *respond = json_object();
    if (!respond) { json_delete(top); return; }    
    json_array_append_new(responses, respond);

    {
        //
        //  responses > response[]
        // 
       
        json_object_set_nocheck(respond, "rid", rid);
        json_object_set_new_nocheck(respond, "stream", json_string("closed"));
        
        //
        //  columns
        //   /new 노드를 생성할때 $columns 메타데이터를 설정했기때문에 
        //   response 에 보낼 필요없다. 
        // 
        // json_t *columns = json_array();
        // if (!columns){ json_delete(top); return; }
        // json_object_set_new_nocheck(respond, "columns", columns);

        // json_t *column_ip = json_object();
        // if (!column_ip){ json_delete(top); return; }
        // json_object_set_new(column_ip, "name", json_string("ip"));
        // json_object_set_new(column_ip, "type", json_string("string"));
        // json_array_append_new(columns, column_ip);

        // json_t *column_host = json_object();
        // if (!column_ip){ json_delete(top); return; }
        // json_object_set_new(column_host, "name", json_string("host"));
        // json_object_set_new(column_host, "type", json_string("string"));    
        // json_array_append_new(columns, column_host);

        //
        //  updates
        // 
        json_t *updates = json_array();
        if (!updates){ json_delete(top); return; }
        json_object_set_new_nocheck(respond, "updates", updates);

        json_t* update = json_array();
        if (!updates){ json_delete(top); return; }
        json_array_append_new(updates, update);

        json_array_append_new(update, json_string(ip));
        json_array_append_new(update, json_string(host_name));
    }   


    #ifdef MYDBG
    {
                char* data = json_dumps(top, JSON_INDENT(2));
        log_info ("resps = \n%s \n", data);
        dslink_free(data);
    }
    #endif//MYDBG

    dslink_ws_send_obj((struct wslay_event_context *) link->_ws, top);
    json_delete(top);
    return;
}





/// @brief  
void eip_responder_init(DSLink *link, DSNode *root) 
{
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
    //  `/new` action node 를 생성하고, invoke 핸들러를 등록한다. 
    //     
    DSNode *new_connection = dslink_node_create(connections->parent, "new", "node");
    if (!new_connection) {
        log_warn("Failed to create new_connection action node\n");
        return;
    }

    new_connection->on_invocation = invoke_new_connection;
    dslink_node_set_meta(link, new_connection, "$name", json_string("Add New Host"));
    dslink_node_set_meta(link, new_connection, "$invokable", json_string("read"));

    //
    //  action 의 $columns 를 정의한다. 
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
    json_object_set_new(param_host, "name", json_string("port"));
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
