/* Jenna Whilden (jpwolf101@gmail.com) 11-22-2021 */
/* Simple HTML web server header */

/*
Program Archetecture:
 -  Parse and validate root folder addr, and optional port, ip and verbose flags
    - Default ip is any avaiable, Default port is auto-assigned
 -  Open initial listening socket for that IP
 -  Print and flush port information to stdout
 -  Use select to wait for a socket to open
 -  Iterate through each ready socket, consulting action FSM to determine work

Socket Action FSM:
 -  If socket is the listener: accept new client and create client with stage 0
 -  If socket is a client, find matching struct:
     -  If current stage is READING, start/continue saving data to struct
         -  If recv'd entire client data, parse the read data, and set stage
            to SENDING
         -  If partial recv, keep stage at READING
     -  If current stage is SENDING, follow below sending logic

Parsing Overview:
 - All inputs must start with "GET /", or else be invalid (500)
 - Next piece is a string "/<!!>"
 - "/<!!>" must be an implemented page, or else be invalid (404)
 - "/<!!>" will be followed by a newline or a space, and all else
   is ignored (Treat everything as HTTP/1.1)
 - "/" is iterpreted as "/index.html"
 - If no extension is provided, assumed to be .html

Sending Logic Overview:
 - Only sockets that immediatly need to be written to are added to select list
 1.   If data_offset == data_size, read in more data from the pipe to data buf
       a.   Set data_size to bytes read and set data_offset to 0
 2.   Attempt to send entire data buf, starting at data_offset
 3.   Increase data_offset by bytes written
 4.   If EOF reached, write is complete, close socket

*/

#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include<stdio.h> /* File* struct */

/* Define contant URLs */
#define WS_URL_INDEX       "/index.html"
#define WS_URL_404         "/err404.html"
#define WS_URL_500         "/err500.html"

/* Define header string */
/* HTTP status (200 OK, 500 OK, 404 Not Found, etc), 
   type (text/html, application/json), content length */
#define WS_STR_CONTENT_HEADER "HTTP/1.0 %s\r\nContent-Type: %s\r\nContent-Length: %lu\r\n\r\n"

/* Define parsing statuses */
#define WS_STATUS_OK        200
#define WS_STATUS_MISSING   404
#define WS_STATUS_INVALID   500

/* Define content type strings */
#define WS_TYPE_UNKNOWN    "application/octet-stream"
#define WS_TYPE_HTML       "text/html"
#define WS_TYPE_CSS        "text/css"
#define WS_TYPE_JS         "text/javascript"
#define WS_TYPE_GIF        "image/gif"
#define WS_TYPE_JPEG       "image/jpeg"
#define WS_TYPE_PNG        "image/png"
#define WS_TYPE_SVG        "image/svg+xml"

/* Define known file extentions */
#define WS_EXT_HTML        ".html"
#define WS_EXT_HTM         ".htm"
#define WS_EXT_CSS         ".css"
#define WS_EXT_JS          ".js"
#define WS_EXT_GIF         ".gif"
#define WS_EXT_JPG         ".jpg"
#define WS_EXT_JPEG        ".jpeg"
#define WS_EXT_PNG         ".png"
#define WS_EXT_SVG         ".svg"
#define WS_EXT_XML         ".xml"

/* Define client stages */
#define WS_STAGE_READING   0
#define WS_STAGE_SENDING   1

/* Define ctoa levels */
#define WS_CTOA_SIMPLE     0
#define WS_CTOA_SOCKET     1
#define WS_CTOA_DATA       2
#define WS_CTOA_FULL       3

/* Define misc */
#define WS_MAX_DATA        (1<<12) /* 4KB for storing in client buffer */
#define WS_MAX_HEADER      (256) /* Max possible len of header */
#define WS_PREFIX_LEN      4
#define WS_DEFAULT_PORT    0
#define USAGE_STR          "Usage: %s root [-v] [-a ip-address] [-p port]\n"
#define HELP_STR           "Simple HTML web server\n" USAGE_STR "\n" \
                           "root\t\tThe path to the root directory of the web server\n" \
                           "-v\t\tEnables verbose output, printing additional client details\n" \
                           "-a <ip-address>\tAn IPv4 or IPv6 address to be used for the web server [defaults to any open]\n" \
                           "-p <port>\tThe port number for accessing the web server [defaults to a random unused port]\n"

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE (!TRUE)
#endif

/* Node in linked list of clients */
struct client_node_t {
    long id;                    /* Unique identifier */
    int socket;                 /* FD of the socket */
    FILE *pipe;                 /* FILE* for pipe to external process */
    int stage;                  /* What the client needs to do */
    int offset;                 /* Current offset in data */
    int data_size;              /* Size of data (to write) */
    char data[WS_MAX_DATA];     /* Data recv'd from socket / to be written */
    struct client_node_t *next; /* Next client node in LL */
};
typedef struct client_node_t client_node_t;
typedef client_node_t* client_node_p;

#endif