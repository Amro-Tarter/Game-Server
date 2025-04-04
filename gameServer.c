#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <signal.h>
#include <errno.h>

#define BUFFER_SIZE 1024
#define MAX_MESSAGE_QUEUE 10
#define MAX_PLAYERS 100

int available_ids[MAX_PLAYERS];
int available_id_count = 0;

typedef struct Player {
    int id;
    int socket;
    char message_queue[MAX_MESSAGE_QUEUE][BUFFER_SIZE];
    int message_count;
    int current_message_offset;
} Player;

typedef struct WaitingPlayer {
    int socket;
    struct WaitingPlayer *next;
} WaitingPlayer;

WaitingPlayer *waiting_queue_head = NULL;
WaitingPlayer *waiting_queue_tail = NULL;

int welcome_socket;
int max_players;
int next_random_number;
Player *players;
fd_set read_fds, write_fds;
int max_fd;
int game_active = 1;

void initialize_available_ids(int max_number_of_players) {
    for (int i = 0; i < max_number_of_players; i++) {
        available_ids[i] = i + 1;
    }
    available_id_count = max_number_of_players;
}

int assign_id() {
    if (available_id_count > 0) {
        int id = available_ids[0];
        for (int i = 1; i < available_id_count; i++) {
            available_ids[i - 1] = available_ids[i];
        }
        available_id_count--;
        return id;
    }
    return -1;
}

void reclaim_id(int id) {
    if (available_id_count < MAX_PLAYERS) {
        available_ids[available_id_count++] = id;
    }
}

void cleanup_and_exit() {
    if (players != NULL) {
        for (int i = 0; i < max_players; i++) {
            if (players[i].socket != -1) {
                close(players[i].socket);
            }
        }
        free(players);
    }

    // Free waiting queue
    while (waiting_queue_head) {
        WaitingPlayer *temp = waiting_queue_head;
        waiting_queue_head = waiting_queue_head->next;
        close(temp->socket);
        free(temp);
    }

    if (welcome_socket != -1) {
        close(welcome_socket);
    }
    exit(EXIT_SUCCESS);
}

void enqueue_player(int client_socket) {
    WaitingPlayer *new_player = malloc(sizeof(WaitingPlayer));
    if (!new_player) {
        perror("Failed to allocate memory for waiting player");
        close(client_socket);
        return;
    }
    new_player->socket = client_socket;
    new_player->next = NULL;

    if (!waiting_queue_tail) {
        waiting_queue_head = waiting_queue_tail = new_player;
    } else {
        waiting_queue_tail->next = new_player;
        waiting_queue_tail = new_player;
    }
}

int dequeue_player() {
    if (!waiting_queue_head) {
        return -1;
    }
    int client_socket = waiting_queue_head->socket;
    WaitingPlayer *temp = waiting_queue_head;
    waiting_queue_head = waiting_queue_head->next;
    if (!waiting_queue_head) {
        waiting_queue_tail = NULL;
    }
    free(temp);
    return client_socket;
}

void signal_handler(int signum) {
    cleanup_and_exit();
}

void initialize_server(int port) {
    welcome_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (welcome_socket < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(port);

    int opt = 1;
    if (setsockopt(welcome_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Setsockopt failed");
        exit(EXIT_FAILURE);
    }

    if (bind(welcome_socket, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(welcome_socket, max_players) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);
    FD_SET(welcome_socket, &read_fds);
    max_fd = welcome_socket;

    players = calloc(max_players, sizeof(Player));
    if (players == NULL) {
        perror("Memory allocation failed");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < max_players; i++) {
        players[i].socket = -1;
        players[i].id = -1;
        players[i].message_count = 0;
        players[i].current_message_offset = 0;
    }
}

void send_message(int socket, const char *message) {
    send(socket, message, strlen(message), 0);
}

void broadcast_message(const char *message, int exclude_id) {
    for (int i = 0; i < max_players; i++) {
        if (players[i].socket != -1 && players[i].id != exclude_id) {
            send_message(players[i].socket, message);
        }
    }
}

void handle_new_connection() {
    struct sockaddr_in client_address;
    socklen_t client_addrlen = sizeof(client_address);
    int client_socket = accept(welcome_socket, (struct sockaddr *)&client_address, &client_addrlen);
    if (client_socket < 0) {
        perror("Accept failed");
        return;
    }

    int assigned_id = assign_id();
    if (assigned_id == -1) {
        enqueue_player(client_socket);
        const char *message = "Server is full, please wait.\n";
        send(client_socket, message, strlen(message), 0);
        return;
    }

    // Find the first available player slot
    int player_index = -1;
    for (int i = 0; i < max_players; i++) {
        if (players[i].socket == -1) {
            player_index = i;
            break;
        }
    }

    players[player_index].socket = client_socket;
    players[player_index].id = assigned_id;
    players[player_index].message_count = 0;
    players[player_index].current_message_offset = 0;

    FD_SET(client_socket, &read_fds);
    if (client_socket > max_fd) {
        max_fd = client_socket;
    }

    char welcome_message[BUFFER_SIZE];
    snprintf(welcome_message, sizeof(welcome_message), "Welcome to the game, your id is %d\n", assigned_id);
    send(client_socket, welcome_message, strlen(welcome_message), 0);

    char join_message[BUFFER_SIZE];
    snprintf(join_message, sizeof(join_message), "Player %d joined the game\n", assigned_id);
    broadcast_message(join_message, assigned_id);
}

void handle_player_disconnection(int player_id) {
    for (int i = 0; i < max_players; i++) {
        if (players[i].id == player_id) {
            int socket = players[i].socket;
            if (socket != -1) {
                close(socket);
                FD_CLR(socket, &read_fds);
                FD_CLR(socket, &write_fds);
                players[i].socket = -1;
                players[i].id = -1;
                players[i].message_count = 0;
                players[i].current_message_offset = 0;
            }

            reclaim_id(player_id);
            break;
        }
    }

    if (game_active) {
        char message[BUFFER_SIZE];
        snprintf(message, sizeof(message), "Player %d disconnected\n", player_id);
        broadcast_message(message, -1);
    }

    // Check for waiting players
    int waiting_socket = dequeue_player();
    if (waiting_socket != -1) {
        int new_id = assign_id();
        if (new_id == -1) {
            close(waiting_socket);
            return;
        }

        // Find the first available player slot
        int player_index = -1;
        for (int i = 0; i < max_players; i++) {
            if (players[i].socket == -1) {
                player_index = i;
                break;
            }
        }

        players[player_index].socket = waiting_socket;
        players[player_index].id = new_id;
        players[player_index].message_count = 0;
        players[player_index].current_message_offset = 0;

        FD_SET(waiting_socket, &read_fds);
        if (waiting_socket > max_fd) {
            max_fd = waiting_socket;
        }

        char welcome_message[BUFFER_SIZE];
        snprintf(welcome_message, sizeof(welcome_message), "Welcome to the game, your ID is %d\n", new_id);
        send(waiting_socket, welcome_message, strlen(welcome_message), 0);

        char join_message[BUFFER_SIZE];
        snprintf(join_message, sizeof(join_message), "Player %d joined the game\n", new_id);
        broadcast_message(join_message, new_id);
    }
}

void handle_guess(int player_id, const char *guess_str) {
    // Validate input
    char *endptr;
    long guess = strtol(guess_str, &endptr, 10);

    // Check for invalid input
    if (*endptr != '\0' && !isspace(*endptr)) {
        return;
    }

    char guess_message[BUFFER_SIZE];
    snprintf(guess_message, sizeof(guess_message), "Player %d guessed %ld\n", player_id, guess);
    broadcast_message(guess_message, -1);

    if (guess < next_random_number) {
        snprintf(guess_message, sizeof(guess_message), "The guess %ld is too low\n", guess);
        for (int i = 0; i < max_players; i++) {
            if (players[i].id == player_id) {
                send_message(players[i].socket, guess_message);
                break;
            }
        }
    } else if (guess > next_random_number) {
        snprintf(guess_message, sizeof(guess_message), "The guess %ld is too high\n", guess);
        for (int i = 0; i < max_players; i++) {
            if (players[i].id == player_id) {
                send_message(players[i].socket, guess_message);
                break;
            }
        }
    } else {
        snprintf(guess_message, sizeof(guess_message), "Player %d wins\n", player_id);
        broadcast_message(guess_message, -1);

        snprintf(guess_message, sizeof(guess_message), "The correct guess is %ld\n", guess);
        broadcast_message(guess_message, -1);

        game_active = 0;
        // Disconnect all active players
        for (int i = 0; i < max_players; i++) {
            if (players[i].socket != -1) {
                handle_player_disconnection(players[i].id);
            }
        }

        // Reset for next game
        next_random_number = rand() % 100 + 1;
        game_active = 1;
    }
}

int main(int argc, char *argv[]) {
    signal(SIGINT, signal_handler);

    // Validate input arguments
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <port> <seed> <max-number-of-players>\n", argv[0]);
        return EXIT_FAILURE;
    }

    // Robust input validation
    char *endptr;
    long port = strtol(argv[1], &endptr, 10);
    if (*endptr != '\0' || port < 1 || port > 65535) {
        fprintf(stderr, "Usage: ./server <port> <seed> <max-number-of-players>\n");
        return EXIT_FAILURE;
    }

    long seed = strtol(argv[2], &endptr, 10);
    if (*endptr != '\0') {
        fprintf(stderr, "Usage: ./server <port> <seed> <max-number-of-players>\n");
        return EXIT_FAILURE;
    }

    long players_count = strtol(argv[3], &endptr, 10);
    if (*endptr != '\0' || players_count < 2 || players_count > MAX_PLAYERS) {
        fprintf(stderr, "Usage: ./server <port> <seed> <max-number-of-players>\n");
        return EXIT_FAILURE;
    }

    max_players = players_count;
    initialize_available_ids(max_players);

    srand(seed);
    next_random_number = rand() % 100 + 1;

    initialize_server(port);

    while (1) {
        fd_set temp_read_fds = read_fds;
        fd_set temp_write_fds = write_fds;

        int activity = select(max_fd + 1, &temp_read_fds, &temp_write_fds, NULL, NULL);
        if (activity < 0) {
            if (errno == EINTR) continue;
            perror("Select failed");
            exit(EXIT_FAILURE);
        }

        if (FD_ISSET(welcome_socket, &temp_read_fds)) {
            handle_new_connection();
        }

        for (int i = 0; i < max_players; i++) {
            if (players[i].socket != -1 && FD_ISSET(players[i].socket, &temp_read_fds)) {
                char buffer[BUFFER_SIZE];
                int valread = read(players[i].socket, buffer, sizeof(buffer) - 1);
                if (valread <= 0) {
                    handle_player_disconnection(players[i].id);
                } else {
                    buffer[valread] = '\0';

                    // Process multiple lines
                    char *line = strtok(buffer, "\n");
                    while (line) {
                        handle_guess(players[i].id, line);
                        line = strtok(NULL, "\n");
                    }
                }
            }
        }
    }

    cleanup_and_exit();
    return 0;
}
