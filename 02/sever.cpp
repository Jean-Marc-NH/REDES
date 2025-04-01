
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <vector>
#include <algorithm>

#define PORT 45000
#define MAX_CLIENTS 10

int main() {
    int server_fd, new_socket, max_sd, activity, valread;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    char buffer[1024];
    fd_set readfds;
    std::vector<int> clients;

    // 1. Crear socket del servidor
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        perror("Error al crear el socket");
        exit(EXIT_FAILURE);
    }

    // 2. Configurar la dirección del servidor
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    // 3. Enlazar el socket
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("Error en bind");
        exit(EXIT_FAILURE);
    }

    // 4. Poner en modo escucha
    if (listen(server_fd, MAX_CLIENTS) < 0) {
        perror("Error en listen");
        exit(EXIT_FAILURE);
    }

    std::cout << "Servidor escuchando en el puerto " << PORT << "..." << std::endl;

    while (true) {
        // Limpiar el conjunto de descriptores
        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);
        max_sd = server_fd;

        // Agregar clientes al conjunto de descriptores
        for (int client : clients) {
            FD_SET(client, &readfds);
            if (client > max_sd) max_sd = client;
        }

        // Esperar actividad en algún socket
        activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);
        if (activity < 0) {
            perror("Error en select");
            continue;
        }

        // Nueva conexión entrante
        if (FD_ISSET(server_fd, &readfds)) {
            new_socket = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen);
            if (new_socket < 0) {
                perror("Error en accept");
                continue;
            }
            std::cout << "Nuevo cliente conectado." << std::endl;
            clients.push_back(new_socket);
        }

        // Revisar actividad en los clientes
        for (auto it = clients.begin(); it != clients.end();) {
            int sd = *it;
            if (FD_ISSET(sd, &readfds)) {
                valread = read(sd, buffer, sizeof(buffer) - 1);
                if (valread <= 0) {
                    std::cout << "Cliente desconectado." << std::endl;
                    close(sd);
                    it = clients.erase(it);
                    continue;
                }

                buffer[valread] = '\0';
                std::cout << "Cliente: " << buffer << std::endl;

                // Si el cliente dice "chau", desconectarlo
                if (strncmp(buffer, "chau", 4) == 0) {
                    std::cout << "Cliente se ha desconectado voluntariamente." << std::endl;
                    close(sd);
                    it = clients.erase(it);
                    continue;
                }

                // Enviar mensaje a todos los demás clientes
                for (int client : clients) {
                    if (client != sd) {
                        send(client, buffer, strlen(buffer), 0);
                    }
                }
            }
            ++it;
        }
    }

    close(server_fd);
    return 0;
}
