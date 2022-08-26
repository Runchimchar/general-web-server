/* Jenna Whilden (jpwolf101@gmail.com) 11-22-2021 */
/* Simple HTML web server */
#include <stdio.h>          /* High level read and write */
#include <stdlib.h>         /* Memory management */
#include <unistd.h>         /* Lower level read and write */
#include <string.h>         /* String parsing */
#include <errno.h>          /* Error handling */
#include <signal.h>         /* Interupt handling */
#include <dirent.h>         /* For testing directories */
#include <sys/select.h>     /* Select */
#include <sys/time.h>       /* CPU / User time */
#include <sys/types.h>      /* Type definitions */
#include <sys/socket.h>     /* Low level sockets */
#include <sys/resource.h>   /* System usage stats */
#include <netinet/in.h>     /* Address structs */
#include <arpa/inet.h>      /* String to address conversions */
#include "web-server.h"     /* JSON server consts and structs */

/* Globals */
static char alive = 1; /* 0 if server is being killed */
static long client_count = 0; /* Total number of clients */
static long req_count = 0; /* Total number of requests */
static long err_count = 0; /* Total number of errors */
static fd_set client_rdset; /* List of clients to read from for select */
static fd_set client_wrset; /* List of clients to write to for select */
static int max_socket = -1; /* Largest socket value */
static client_node_t _client_head; /* Head of list of clients */
static char* root = NULL; /* Where html pages are stored */
static char ctoabuf[512]; /* Used in pc function */
static int ctoa_level = WS_CTOA_SIMPLE; /* Amount of output from ctoa() */
static char verbose = FALSE; /* Used to determine level of output */

/* Aliases */
#define client_head (&_client_head) /* Alias, useful to have pointer */
#define server _client_head /* Alias, as head will always be listener */
#define ctoa(CLIENT) ctoa_l((CLIENT),ctoa_level)
#define vprint(...) if (verbose) printf(__VA_ARGS__)

/* Intr handler, mostly ftom GT */
void intr_handler(int sig) {
    /* If intr is sigint, exit fish main, else ignore */
    if (sig == SIGINT) {
        alive = 0;
    }
}

/* Returns a string of data from a client struct */
char *ctoa_l(client_node_p client, int ctoa_level) {
    switch (ctoa_level) {
        default:
        case WS_CTOA_SIMPLE:
            sprintf(ctoabuf,"id = %ld", 
                client->id);
            break;
        case WS_CTOA_SOCKET:
            sprintf(ctoabuf,"id = %ld, socket = %d", 
                client->id, client->socket);
            break;
        case WS_CTOA_DATA:
            sprintf(ctoabuf,"id=%ld, socket=%d, offset=%d, data_size=%d", 
                client->id, client->socket, client->offset, client->data_size);
            break;
        case WS_CTOA_FULL:
            sprintf(ctoabuf,"addr=%p, id=%ld, socket=%d, pipe=%p, offset=%d, data_size=%d, next=%p", 
                client, client->id, client->socket, client->pipe, client->offset, client->data_size, client->next);
            break;
    }
    return ctoabuf;
}

/* Add a new client to the system */
void add_client(int socket) {
    /* Update max socket */
    if (socket > max_socket) max_socket = socket;

    /* If server, then done */
    if (socket == server.socket) return;

    /* Allocate node */
    client_node_p node = malloc(sizeof(client_node_t));
    node->id = ++client_count;
    node->socket = socket;
    node->pipe = NULL;
    node->stage = WS_STAGE_READING;
    node->offset = 0;
    node->data_size = 0;
    node->next = NULL;

    /* Add to linked list */
    client_node_p curr = client_head;
    while (curr->next) {curr = curr->next;}
    curr->next = node;

    /* Print and return */
    printf("Added new client{%s}\n",ctoa(node));
}

/* Remove a client from the system. Returns previous node */
client_node_p rm_client(int socket) {
    /* Shutdown client */
    shutdown(socket, SHUT_RDWR);
    close(socket);

    /* Safety check */
    if (socket == server.socket) return NULL;

    /* Remove from linked list */
    client_node_p prev = client_head;
    client_node_p node = NULL;
    while (prev->next->socket != socket) {prev = prev->next;}
    node = prev->next;
    prev->next = node->next;

    /* Close pipe if needed */
    if (node->pipe != NULL) {
        fclose(node->pipe);
    }

    /* Find new max socket */
    client_node_p curr = NULL;
    if (max_socket == socket) {
        max_socket = server.socket;
        curr = client_head;
        while (curr != NULL) {
            if (curr->socket > max_socket) 
                max_socket = curr->socket;
            curr = curr->next;
        }
    }

    /* Print removal notice */
    printf("Removed client{%s}\n",ctoa(node));

    /* Free node */
    free(node);

    return prev;
}

/* Re-add all clients to client set */
void refresh_client_set() {
    /* Clear lists */
    FD_ZERO(&client_rdset);
    FD_ZERO(&client_wrset);

    /* Add all clients to needed set */
    client_node_p curr = client_head;
    while (curr) {
        //fprintf(stderr,"Refreshed socket %d\n",curr->socket);
        if (curr->stage == WS_STAGE_READING) {
            FD_SET(curr->socket, &client_rdset);
        } else if (curr->stage == WS_STAGE_SENDING) {
            FD_SET(curr->socket, &client_wrset);
        }
        curr = curr->next;
    }
}

// Returns the total size of the virtual address space for the running linux process (jbellardo)
long get_memory_usage_linux() {
    // Variables to store all the contents of the stat file
    int pid, ppid, pgrp, session, tty_nr, tpgid;
    char comm[2048], state;
    unsigned int flags;
    unsigned long minflt, cminflt, majflt, cmajflt, vsize;
    unsigned long utime, stime;
    long cutime, cstime, priority, nice, num_threads, itrealvalue, rss;
    unsigned long long starttime;
    // Open the file
    FILE *stat = fopen("/proc/self/stat", "r");
    if (!stat) {
        perror("Failed to open /proc/self/stat");
        return 0;
    }
    // Read the statistics out of the file
    fscanf(stat, "%d%s%c%d%d%d%d%d%u%lu%lu%lu%lu"
    "%ld%ld%ld%ld%ld%ld%ld%ld%llu%lu%ld",
    &pid, comm, &state, &ppid, &pgrp, &session, &tty_nr,
    &tpgid, &flags, &minflt, &cminflt, &majflt, &cmajflt,
    &utime, &stime, &cutime, &cstime, &priority, &nice,
    &num_threads, &itrealvalue, &starttime, &vsize, &rss);
    fclose(stat);
    return vsize;
}

/* Puts a correct url path into buf */
void build_url(char *buf, char *tail) {
    int root_len = strlen(root);
    strcpy(buf, root);
    strcpy(buf+root_len, tail);
    if (strchr(tail, '.') == NULL) {
        strcat(buf, ".html");
    }
    vprint("Built url %s from %s\n",buf,tail);
}

/* Parses a client's data and writes the correct response to its data */
void parse_data(client_node_p client) {
    vprint("Parse started for client{%s}\n", ctoa(client));
    /* Setup vars */
    int parse_type = WS_STATUS_INVALID;
    char *data = client->data;
    char *url_tail = WS_URL_500;
    char url[WS_MAX_DATA];

    /* Safety cutoff */
    data[WS_MAX_DATA-1] = '\n';

    /* Determine type of output */
    if (client->data_size < WS_PREFIX_LEN+1 
            || memcmp(data,"GET /",WS_PREFIX_LEN+1) != 0) {
        /* Check for invalid start */
        url_tail = WS_URL_500;
    } else {
        /* Find end of URL */
        url_tail = data+WS_PREFIX_LEN;
        int len = strcspn(data+WS_PREFIX_LEN, " \n\r");
        
        /* Cut-off string */
        data[WS_PREFIX_LEN + len] = '\0';
        vprint("Parsed url: %s\n",url_tail);
        /* Rest of string is garbage */

        /* Simple malicious url handling */
        if (strstr(url_tail,"..")) {
            url_tail = WS_URL_500;
            vprint("URL was dangerous, 500 sent\n");
        }

        /* Handle index */
        if (strcmp(url_tail,"/") == 0) {
            url_tail = WS_URL_INDEX;
        }
        parse_type = WS_STATUS_OK;
    }

    /* Try and open page's file */
    build_url(url, url_tail);
    FILE *page = fopen(url,"r");
    if (!page) {
        vprint("Page %s is missing!\n", url);
        perror("Open failed");
        build_url(url, WS_URL_404);
        page = fopen(url,"r");
        parse_type = WS_STATUS_MISSING;
    }
    client->pipe = page;

    /* Write file into intermediate buffer */
    char content[WS_MAX_DATA-WS_MAX_HEADER+1]; /* Enough for initial read + null term */
    long unsigned int page_size = fread(content, sizeof(char), WS_MAX_DATA-WS_MAX_HEADER, page);
    vprint("PAGE_SIZE=%ld\n",page_size);
    /* End string */
    content[page_size] = '\0';

    /* Get header status text */
    char *header_status = NULL;
    if (parse_type == WS_STATUS_INVALID) {
        header_status = "500 OK";
        err_count++;
    } else if (parse_type == WS_STATUS_MISSING) {
        header_status = "404 Not Found";
        err_count++;
    } else {
        header_status = "200 OK";
    }
    
    /* Get content type text */
    char *ext = strchr(url,'.');
    char *content_type;
    if (strstr(ext,WS_EXT_HTML) == ext ||
            strstr(ext,WS_EXT_HTM) == ext) {
        content_type = WS_TYPE_HTML;
    } else if (strstr(ext,WS_EXT_CSS) == ext) {
        content_type = WS_TYPE_CSS;
    } else if (strstr(ext,WS_EXT_JS) == ext) {
        content_type = WS_TYPE_JS;
    } else if (strstr(ext,WS_EXT_GIF) == ext) {
        content_type = WS_TYPE_GIF;
    } else if (strstr(ext,WS_EXT_JPG) == ext ||
            strstr(ext,WS_EXT_JPEG) == ext) {
        content_type = WS_TYPE_JPEG;
    } else if (strstr(ext,WS_EXT_PNG) == ext) {
        content_type = WS_TYPE_PNG;
    } else if (strstr(ext,WS_EXT_SVG) == ext ||
            strstr(ext,WS_EXT_XML) == ext) {
        content_type = WS_TYPE_SVG;
    } else {
        content_type = WS_TYPE_UNKNOWN;
    }

    /* Determine file size */
    long int bookmark = ftell(page);
    fseek(page, 0L, SEEK_END);
    unsigned long int content_size = ftell(page);
    fseek(page, bookmark, SEEK_SET);

    /* Assemble header */
    sprintf(data, WS_STR_CONTENT_HEADER, header_status, 
        content_type, content_size);
    
    /* Write start of content after that */
    int dlen = strlen(data);
    memcpy(data+strlen(data), content, page_size);

    /* Update data size and rst offset*/
    client->data_size = page_size+dlen;
    client->offset = 0;

    printf("Client{%s} accessed url %s, with size of %lu bytes\n",ctoa(client),url,content_size);
}

/* Running logic */
int main(int argc, char *argv[]) {
    /* Setup main vars */
    struct sockaddr *address;
    struct sockaddr_in address4;
    struct sockaddr_in6 address6;
    socklen_t addr_len;
    char *addr_str = NULL;
    char addr_ver = AF_INET;
    int port = WS_DEFAULT_PORT;

    /* Parse args */
    if (argc < 2 || argc > 7) {
        printf(USAGE_STR,argv[0]);
        return 0;
    } else {
        /* Get root */
        root = argv[1];

        /* Check for help */
        if (strcmp(root,"--help") == 0) {
            printf(HELP_STR,argv[0]);
            return 0;
        }

        for (int i=2; i < argc; i++) {
            if (strcmp(argv[i],"-a") == 0) {
                /* Ensure value was given */
                if (argc == i+1) {
                    printf(USAGE_STR,argv[0]);
                    return 0;
                }
                addr_str = argv[i+1];
                i++;
            } else if (strcmp(argv[i],"-p") == 0) {
                /* Ensure value was given */
                if (argc == i+1) {
                    printf(USAGE_STR,argv[0]);
                    return 0;
                }
                port = atoi(argv[i+1]);
                i++;
            } else if (strcmp(argv[i],"-v") == 0) {
                verbose = TRUE;
                ctoa_level = WS_CTOA_SOCKET;
            } else {
                printf(USAGE_STR,argv[0]);
                return 0;
            }
        }
    }

    /* Check for root folder access */
    DIR *rootdir = opendir(root);
    if (rootdir == NULL) {
        perror("Couldn't access root folder");
        return errno;
    } else {
        closedir(rootdir);
    }

    /* Install intrupt handler */
    struct sigaction sig; /* For setting up intr handler */
    sig.sa_handler = intr_handler;
    sigfillset(&sig.sa_mask);
    sig.sa_flags = 0;
    if (sigaction(SIGINT, &sig, NULL)) {
        perror("Couldn't set signal handler for SIGINT");
        return errno;
    }

    /* Configure server address and port, accounting for IPv6 */
    if (addr_str) {
        if (inet_pton(AF_INET, addr_str, &(address4.sin_addr))) {
            addr_ver = AF_INET;
            address4.sin_family = AF_INET;
            address4.sin_port = htons( port );
            address = (struct sockaddr *)&address4;
            addr_len = sizeof(address4);
        } else if (inet_pton(AF_INET6, addr_str, &(address6.sin6_addr))) {
            addr_ver = AF_INET6;
            address6.sin6_family = AF_INET6;
            address6.sin6_port = htons( port );
            address = (struct sockaddr *)&address6;
            addr_len = sizeof(address6);
        } else {
            fprintf(stderr,"Error binding TCP socket: Cannot assign requested address\n");
            return 1;
        }
    } else {
        addr_ver = AF_INET;
        address4.sin_family = AF_INET;
        address4.sin_addr.s_addr = INADDR_ANY;
        address4.sin_port = htons( port );
        address = (struct sockaddr *)(&address4);
        addr_len = sizeof(address4);
    }

    /* Create and configure socket */
    server.socket = socket(addr_ver, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (server.socket == -1) {
        perror("Server socket creation error");
        return errno;
    }
    // Do something to enable simultaneous v6?

    /* Bind the server to the port */
    if (bind(server.socket, address, addr_len) < 0) {
        perror("Server binding error");
        return errno;
    }

    /* Enable listening mode on the server */
    if (listen(server.socket, 10) < 0) {
        perror("Server listening error");
        return errno;
    }

    /* Print and flush socket info */
    getsockname(server.socket, address, &addr_len);
    if (addr_ver == AF_INET6) {
        port = ((struct sockaddr_in6 *) address)->sin6_port;
    } else {
        port = ((struct sockaddr_in *) address)->sin_port;
    }
    fprintf(stdout,"HTTP server is using TCP port %d\nHTTPS server is using TCP port -1\n", ntohs(port));
    fflush(stdout);

    /* Setup initial client set */
    add_client(server.socket);
    server.stage = WS_STAGE_READING;
    vprint("Listener with socket %d ready\n",server.socket);

    /* Main event loop */
    while (alive) {
        //vprint("EVENT LOOP TOP REACHED\n");
        /* Setup for select */
        //struct timeval timeout = { 1, 0 }; /* 1s timeout */
        refresh_client_set();

        /* Wait for clients */
        int i = select(max_socket+1, &client_rdset, &client_wrset, NULL, NULL);

        /* Check for timeout */
        if (i == 0) {
            vprint("EVENT LOOP TIMEOUT\n");
            continue;
        }
        //vprint("%d ready descriptor(s)\n",i);

        /* Serve current clients */
        client_node_p curr = client_head->next;
        while (curr != NULL && i > 0 && alive) {
            /* Determine client needs */
            if (FD_ISSET(curr->socket, &client_rdset)) {
                /* Mark that a socket was used */
                i--;
                /* Read in a chunk of data */
                vprint("Client{%s} started read\n",ctoa(curr));
                int diff = read(curr->socket, 
                    curr->data+curr->data_size, WS_MAX_DATA-curr->data_size);
                curr->data_size += diff;
                vprint("Client{%s} read %d bytes\n",ctoa(curr),diff);

                /* Check if empty read (socket closed) */
                if (diff == 0) {
                    printf("Client{%s} closed remotly\n",ctoa(curr));
                    curr = rm_client(curr->socket);
                } else if (curr->data[curr->data_size-1] == '\n' /* Check if read completed */
                        || curr->data_size == WS_MAX_DATA) {
                    /* Parse the read data */
                    req_count++;
                    parse_data(curr);

                    /* Set stage to sending */
                    curr->stage = WS_STAGE_SENDING;
                }
            } else if (FD_ISSET(curr->socket, &client_wrset)) {
                /* Mark that a socket was used */
                i--;
                /* Check if all current data has been written */
                if (curr->offset >= curr->data_size) {
                    curr->offset = 0;
                    curr->data_size = fread(curr->data, sizeof(char), 
                        WS_MAX_DATA, curr->pipe);
                }

                /* Send as much of the remaining data as possible */
                int bytes_sent = send(curr->socket, curr->data+curr->offset, 
                    curr->data_size-curr->offset, 0);
                curr->offset += bytes_sent;
                vprint("Client{%s} sent %d bytes\n",ctoa(curr),bytes_sent);
                
                /* If EOF reached, close connection and clean up */
                if (feof(curr->pipe)) {
                    curr = rm_client(curr->socket); /* Allows accessing next */
                }
            }

            /* Move on to next client */
            curr = curr->next;
        }

        /* Accept new clients */
        if (i > 0 && FD_ISSET(server.socket, &client_rdset)) {
            int new_socket = accept(server.socket, address, &addr_len);
            if (new_socket != -1) {
                add_client(new_socket);
            } else {
                perror("Client failed to connect\n");
                err_count++;
            }
        }
    }
    
    /* Cleanup and shutdown everything */
    client_node_p curr = client_head;
    client_node_p next = NULL;
    while (curr) {
        next = curr->next;
        rm_client(curr->socket);
        curr = next;
    }

    printf("Server exiting cleanly.\n");
    return 0;
}