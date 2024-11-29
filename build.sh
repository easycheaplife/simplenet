#!/bin/bash
g++ -g -o server epoll_reactor.cc event_loop.cc select_reactor.cc tcp_connection.cc server.cc -lpthread
g++ -g -o echo_server epoll_reactor.cc event_loop.cc select_reactor.cc tcp_connection.cc echo_server.cc -lpthread
g++ -g -o test_client test_client.cc
g++ -g -o stress_test stress_test.cc -lpthread
