#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>

#define BUFF_LEN 63
#define MSG_BUFF_LEN 256

void close_all_sockets(SOCKET* sock_1, SOCKET* sock_2, SOCKET* sock_3, SOCKET* sock_4)
{
	closesocket(*sock_1);
	closesocket(*sock_2);
	closesocket(*sock_3);
	closesocket(*sock_4);
}

int add_noise_to_data(char* buff, int size, int seed, double probability)
{
	double			random_num;
	int				i, j;
	int				bits_flipped = 0;
	unsigned char	mask = 1;

	for (i = 0; i < size; i++)
	{
		for (j = 0; j < 8; j++)
		{
			random_num = (double)rand() / RAND_MAX;
			if ((probability == 1.0) || (random_num - probability < 0.0))
			{
				buff[i] ^= (mask << j);
				bits_flipped++;
			}
		}
	}

	return bits_flipped;
}

int main(int argc, char** argv)
{
	/*initializes winsock*/
	WSADATA wsaData;
	int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != NO_ERROR)
		printf("Error at WSAStartup()\n");

	/*checks arguments*/
	if (argc != 5)
	{
		fprintf(stderr, "Error: the channel should get 4 arguments");
		WSACleanup();
		return -1;
	}

	int					sender_port = strtol(argv[1], NULL, 10);
	int					receiver_port = strtol(argv[2], NULL, 10);
	double				probability = strtod(argv[3], NULL);
	int					seed = strtol(argv[4], NULL, 10);
	int					status;
	int					bytes_received;
	int					total_bytes;
	int					to_send;
	int					bytes_sent;
	int					current_bytes_sent;
	int					bits_flipped;
	struct sockaddr_in	my_addr_for_sender;
	struct sockaddr_in  my_addr_for_receiver;
	struct sockaddr_in  sender_addr;
	struct sockaddr_in  receiver_addr;
	char				buff[BUFF_LEN];
	char				msg_buff[MSG_BUFF_LEN];
	char*				sender_ip_buff;
	char*				receiver_ip_buff;

	/*creates socket for sender*/
	SOCKET sender_listener_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sender_listener_sock == INVALID_SOCKET)
	{
		fprintf(stderr, "Error creating socket: %ld\n", WSAGetLastError());
		WSACleanup();
		return -1;
	}

	/*creates socket for receiver*/
	SOCKET receiver_listener_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (receiver_listener_sock == INVALID_SOCKET)
	{
		fprintf(stderr, "Error creating socket: %ld\n", WSAGetLastError());
		closesocket(sender_listener_sock);
		WSACleanup();
		return -1;
	}

	my_addr_for_sender.sin_family = AF_INET;
	my_addr_for_sender.sin_addr.s_addr = INADDR_ANY;
	my_addr_for_sender.sin_port = htons(sender_port);

	/*binds sender_listener_socket*/
	status = bind(sender_listener_sock, (SOCKADDR*)&my_addr_for_sender, sizeof(my_addr_for_sender));
	if (status == SOCKET_ERROR)
	{
		fprintf(stderr, "Error binding to socket: %ld\n", WSAGetLastError());
		closesocket(sender_listener_sock);
		closesocket(receiver_listener_sock);
		WSACleanup();
		return -1;
	}

	/*starts listening on sender port*/
	status = listen(sender_listener_sock, 1);
	if (status == SOCKET_ERROR)
	{
		fprintf(stderr, "Error binding to socket: %ld\n", WSAGetLastError());
		closesocket(sender_listener_sock);
		closesocket(receiver_listener_sock);
		WSACleanup();
		return -1;
	}

	my_addr_for_receiver.sin_family = AF_INET;
	my_addr_for_receiver.sin_addr.s_addr = INADDR_ANY;
	my_addr_for_receiver.sin_port = htons(receiver_port);

	/*binds receiver_listener_socket*/
	status = bind(receiver_listener_sock, (SOCKADDR*)&my_addr_for_receiver, sizeof(my_addr_for_receiver));
	if (status == SOCKET_ERROR)
	{
		fprintf(stderr, "Error binding to socket: %ld\n", WSAGetLastError());
		closesocket(sender_listener_sock);
		closesocket(receiver_listener_sock);
		WSACleanup();
		return -1;
	}

	/*starts listening to receiver_port*/
	status = listen(receiver_listener_sock, 1);
	if (status == SOCKET_ERROR)
	{
		fprintf(stderr, "Error binding to socket: %ld\n", WSAGetLastError());
		closesocket(sender_listener_sock);
		closesocket(receiver_listener_sock);
		WSACleanup();
		return -1;
	}

	/*accepts receiver*/
	SOCKET receiver_sock = accept(receiver_listener_sock, (SOCKADDR*)&receiver_addr, NULL);
	if (receiver_sock == INVALID_SOCKET)
	{
		fprintf(stderr, "Error accepting receiver socket: %ld\n", WSAGetLastError());
		closesocket(sender_listener_sock);
		closesocket(receiver_listener_sock);
		WSACleanup();
		return -1;
	}

	/*accepts sender*/
	SOCKET sender_sock = accept(sender_listener_sock, (SOCKADDR*)&sender_addr, NULL);
	if (sender_sock == INVALID_SOCKET)
	{
		fprintf(stderr, "Error accepting sender socket: %ld\n", WSAGetLastError());
		closesocket(receiver_sock);
		closesocket(sender_listener_sock);
		closesocket(receiver_listener_sock);
		WSACleanup();
		return -1;
	}

	bits_flipped = 0;
	total_bytes = 0;
	bytes_received = 0;
	srand(seed);

	/*receives data from sender and sends to receiver*/
	while ((bytes_received = recv(sender_sock, buff, BUFF_LEN, 0)) > 0)
	{
		total_bytes += bytes_received;
		bits_flipped += add_noise_to_data(buff, bytes_received, seed, probability);
		to_send = bytes_received;
		bytes_sent = 0;
		current_bytes_sent = 0;
		while (to_send > 0)
		{
			current_bytes_sent = send(receiver_sock, buff + bytes_sent, bytes_received - bytes_sent, 0);
			if (current_bytes_sent == SOCKET_ERROR)
			{
				fprintf(stderr, "Error sending data to receiver: %ld\n", WSAGetLastError());
				close_all_sockets(&sender_listener_sock, &receiver_listener_sock, &sender_sock, &receiver_sock);
				WSACleanup();
				return -1;
			}

			bytes_sent += current_bytes_sent;
			to_send -= current_bytes_sent;
		}
	}

	/*checks if we exited the while loop because of an error*/
	if (bytes_received == SOCKET_ERROR)
	{
		fprintf(stderr, "Error receiving data from sender: %ld\n", WSAGetLastError());
		close_all_sockets(&sender_listener_sock, &receiver_listener_sock, &sender_sock, &receiver_sock);
		WSACleanup();
		return -1;
	}

	/*if we got here, the sender closed the socket for sending data*/
	status = shutdown(receiver_sock, SD_SEND);
	if (status == SOCKET_ERROR)
	{
		fprintf(stderr, "Error closing receiver socket for writing: %ld\n", WSAGetLastError());
		close_all_sockets(&sender_listener_sock, &receiver_listener_sock, &sender_sock, &receiver_sock);
		WSACleanup();
		return -1;
	}

	/*gets final response from receiver*/
	bytes_received = 0;
	while ((bytes_received = recv(receiver_sock, msg_buff, MSG_BUFF_LEN, 0)) > 0)
	{
		to_send = bytes_received;
		bytes_sent = 0;

		/*sends receiver's message to sender*/
		while (to_send > 0)
		{
			current_bytes_sent = send(sender_sock, msg_buff + bytes_sent, to_send, 0);
			if (current_bytes_sent == SOCKET_ERROR)
			{
				fprintf(stderr, "Error sending data to sender: %ld\n", WSAGetLastError());
				close_all_sockets(&sender_listener_sock, &receiver_listener_sock, &sender_sock, &receiver_sock);
				WSACleanup();
				return -1;
			}

			bytes_sent += current_bytes_sent;
			to_send -= current_bytes_sent;
		}
	}

	/*closes all sockets*/
	close_all_sockets(&sender_listener_sock, &receiver_listener_sock, &sender_sock, &receiver_sock);

	/*checks if we exited the while loop because of an error, in that case we print an error occurred and exit with -1*/
	if (bytes_received == SOCKET_ERROR)
	{
		fprintf(stderr, "Error receiving data from receiver: %ld\n", WSAGetLastError());
		WSACleanup();
		return -1;
	}

	/*if we got here, no error occurred*/
	/*we print a final message*/
	sender_ip_buff = inet_ntoa(sender_addr.sin_addr);
	receiver_ip_buff = inet_ntoa(receiver_addr.sin_addr);
	fprintf(stderr, "sender: %s\nreceiver: %s\n%d bytes flipped %d bits\n", sender_ip_buff, receiver_ip_buff, total_bytes, bits_flipped);

	WSACleanup();
	return 0;
}