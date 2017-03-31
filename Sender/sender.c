#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>

#define READ_BUFF_SIZE 57
#define SEND_BUFF_SIZE 63
#define	MID_READ_BUFF_SIZE (57 * 8)
#define MID_SEND_BUFF_SIZE (63 * 8)
#define MSG_SIZE 256
#define	NUM_OF_CHECKBITS 6

static const unsigned int checkbits[NUM_OF_CHECKBITS + 1] = { 1, 2, 4, 8, 16, 32, 64 };

/* Auxiliary function
 *
 * Converts each bit in bits_buff to a byte in bytes_buff.
 */
void bits_to_bytes(char* bits_buff, char* bytes_buff, int size) {
	int i, j, k = 0;
	unsigned char mask = 1;
	unsigned char byte = 0;

	for (i = 0; i < size; i++)
	{
		byte = bits_buff[i];
		for (j = 0; j < 8; j++)
		{
			bytes_buff[k] = mask & byte;
			k++;
			byte >>= 1;
		}
	}
}

/* Auxiliary function
*
* Converts each byte in byte_buff to a bit in bit_buff.
*/
void bytes_to_bits(char* bits_buff, char* bytes_buff, int size) {
	int i, j, k = 0;
	unsigned char mask = 1;
	unsigned char byte = 0;

	for (i = 0; i < size; i++)
	{
		for (j = 0; j < 8; j++)
		{
			byte |= (bytes_buff[k++] << j);
		}
		bits_buff[i] = byte;
		byte = 0;
	}
}

/* Auxiliary function
*
* Calculates hamming code (63,57) and assigns outcome to out_buff.
*/
void calculate_hamming_code(char* in_buff, char* out_buff, unsigned int in_size, unsigned int out_size) {
	unsigned int i, j, k, l = 0, offset_out, offset_in;
	unsigned char mask = 1;
	unsigned char check = 0;

	for (i = 0; i < 8; i++)
	{
		offset_out = out_size * i;
		offset_in = in_size * i;
		mask = 1;
		l = 0;
		for (j = 0; j < NUM_OF_CHECKBITS; j++)
		{
			mask = checkbits[j];
			/* Mapping data bits from 57 bits of data to 63 bits array with redundancy */
			for (k = checkbits[j]; k < checkbits[j+1] - 1; k++)
				out_buff[offset_out + k] = in_buff[offset_in + l++];
		}

		for (j = 0; j < NUM_OF_CHECKBITS; j++)
		{
			mask = checkbits[j];
			check = 0;
			for (k = 0; k < out_size; k++)
			{
				/* Calculates check bits */
				if ((k + 1) != mask && ((k + 1) & mask))
					check ^= out_buff[offset_out + k];
			}
			out_buff[offset_out + checkbits[j] - 1] = check;
		}
	}

}

int main(int argc, char** argv)
{

	if (argc != 4)
	{
		fprintf(stderr, "Wrong number of cmd-line args: %d\n", argc);
		return -1;
	}

	/* Initializes winsock */
	WSADATA wsaData;
	int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != NO_ERROR)
		printf("Error at WSAStartup()\n");

	int					status;
	FILE*				input_file = NULL;
	int					remote_port;
	struct sockaddr_in	remote_addr;
	char				read_buff[READ_BUFF_SIZE]		= { 0 };
	char				send_buff[SEND_BUFF_SIZE]		= { 0 };
	char				mid_read[MID_READ_BUFF_SIZE]	= { 0 };
	char				mid_send[MID_SEND_BUFF_SIZE]	= { 0 };
	int					bytes_read = 0;
	int					bytes_sent = 0;
	int					to_send = 0;
	int					to_read = READ_BUFF_SIZE;
	int					bytes = 1;
	char				info_buff[MSG_SIZE]	= { 0 };
	int					received	= 0;
	int					recons		= 0;
	int					corrected	= 0;

	SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock == INVALID_SOCKET)
	{
		fprintf(stderr, "Error creating socket: %ld\n", WSAGetLastError());
		WSACleanup();
		return -1;
	}

	/* Sets up remote parameters */
	remote_port = strtol(argv[2], NULL, 10);
	remote_addr.sin_family = AF_INET;
	remote_addr.sin_port = htons(remote_port);
	remote_addr.sin_addr.s_addr = inet_addr(argv[1]);

	/* Connecting to remote host */
	status = connect(sock, (SOCKADDR*)&remote_addr, sizeof(remote_addr));
	if (status == SOCKET_ERROR)
	{
		fprintf(stderr, "Error connecting to peer socket: %ld\n", WSAGetLastError());
		closesocket(sock);
		WSACleanup();
		return -1;
	}

	/* Opening file for binary reading */
	input_file = fopen(argv[3], "rb");
	if (!input_file)
	{
		fprintf(stderr, "Error opening file for binary read\n");
		closesocket(sock);
		WSACleanup();
		return -1;
	}

	while (1)
	{
		/* Reading data from file */
		to_read = READ_BUFF_SIZE;
		bytes_read = 0;

		while (to_read > 0 && bytes != 0)
		{
			bytes = fread(read_buff + bytes_read, 1, to_read, input_file);
			if (bytes < 0)
			{
				fprintf(stderr, "Error reading from file\n");
				fclose(input_file);
				closesocket(sock);
				WSACleanup();
				return -1;
			}
			to_read -= bytes;
			bytes_read += bytes;
		}

		if (bytes == 0)
			break;

		/* Encoding */
		bits_to_bytes(read_buff, mid_read, READ_BUFF_SIZE);
		calculate_hamming_code(mid_read, mid_send, READ_BUFF_SIZE, SEND_BUFF_SIZE);
		bytes_to_bits(send_buff, mid_send, SEND_BUFF_SIZE);

		/* Sending to receiver */
		to_send = SEND_BUFF_SIZE;
		bytes_sent = 0;

		while (to_send > 0)
		{
			bytes = send(sock, send_buff + bytes_sent, to_send, 0);
			if (bytes == SOCKET_ERROR)
			{
				fprintf(stderr, "Error sending to receiver: %ld\n", WSAGetLastError());
				fclose(input_file);
				closesocket(sock);
				WSACleanup();
				return -1;
			}
			to_send -= bytes;
			bytes_sent += bytes;
		}
	}

	/* Sending FIN flag */
	status = shutdown(sock, SD_SEND);
	if (status == SOCKET_ERROR)
	{
		fprintf(stderr, "Error closing socket for writing: %ld\n", WSAGetLastError());
		fclose(input_file);
		WSACleanup();
		return -1;
	}

	to_read = MSG_SIZE;
	bytes_read = 0;

	/* Waiting for info about transmition and error correction from receiver */
	while ((bytes = recv(sock, info_buff + bytes_read, to_read, 0)) > 0)
	{
		bytes_read += bytes;
		to_read -= bytes;
	}

	if (bytes == SOCKET_ERROR)
	{
		fprintf(stderr, "Error receiving info to print: %ld\n", WSAGetLastError());
		fclose(input_file);
		closesocket(sock);
		WSACleanup();
		return -1;
	}

	/* Closing connection */
	closesocket(sock);
	fclose(input_file);

	sscanf(info_buff, "%d:%d:%d:", &received, &recons, &corrected);
	fprintf(stderr, "received: %d bytes\n", received);
	fprintf(stderr, "reconstructed: %d bytes\n", recons);
	fprintf(stderr, "corrected: %d bytes\n", corrected);

	WSACleanup();

	return 0;

}