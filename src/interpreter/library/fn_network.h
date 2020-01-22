FNDEF3(network_create_server_raw, type, port, max_client)
FNDEF3(network_create_server, type, port, max_client)
FNDEF1(network_create_socket, type)
FNDEF2(network_create_socket_ext, type, port)
FNDEF3(network_connect_raw, socket, url, port)
FNDEF3(network_connect, socket, url, port)
//FNDEF1(network_connect_resolve, url)
//FNDEF2(network_set_config, config, value)
//FNDEF2(network_set_timeout, read, write)
//FNDEF3(network_send_broadcast, port, buffer, size)
FNDEF3(network_send_raw, socket, buffer, size)
ALIAS(network_send_raw, network_send_packet)
FNDEF5(network_send_udp_raw, socket, url, port, buffer, size)
ALIAS(network_send_upd_raw, network_send_udp)
FNDEF1(network_destroy, socket)

CONST(network_socket_tcp, 0)
CONST(network_socket_udp, 1)
CONST(network_socket_bluetooth, 2)

CONST(network_config_connect_timeout, 0)
CONST(network_config_use_non_blocking_socket, 1)

CONST(network_type_connect, static_cast<int32_t>(SocketEvent::CONNECTION_ACCEPTED))
CONST(network_type_disconnect, static_cast<int32_t>(SocketEvent::CONNECTION_ENDED))
CONST(network_type_data, static_cast<int32_t>(SocketEvent::DATA_RECEIVED))
CONST(network_type_non_blocking_connect, static_cast<int32_t>(SocketEvent::CONNECTION_ACCEPTED))
