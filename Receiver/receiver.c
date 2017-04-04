#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>

#define RECV_BUFF_SIZE 63
#define WRITE_BUFF_SIZE 57
#define	MID_RECV_BUFF_SIZE (63 * 8)
#define MID_WRITE_BUFF_SIZE (57 * 8)
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
			bytes_buff[k++] = mask & byte;
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
* Decodes hamming code (63,57) and assigns outcome to out_buff.
*/
int decode_hamming(char* in_buff, char* out_buff, unsigned int in_size, unsigned int out_size) {
	unsigned int i, j, k, l = 0, offset_out, offset_in;
	unsigned char mask = 1;
	unsigned char check = 0;
	int error_bit = 0;
	int corrected = 0;

	for (i = 0; i < 8; i++)
	{
		offset_out = out_size * i;
		offset_in = in_size * i;
		for (j = 0; j < NUM_OF_CHECKBITS; j++)
		{
			mask = checkbits[j];
			check = 0;
			for (k = 0; k < in_size; k++)
			{
				/* XORing all releveant data & check bits to check for errors */
				if ((k + 1) & mask)
					check ^= in_buff[offset_in + k];
			}
			if (check)
				error_bit += checkbits[j];
		}

		if (error_bit) {
			/* Correcting error */
			in_buff[offset_in + error_bit - 1] ^= 0x1;
			corrected++;
		}

		error_bit = 0;

		for (j = 0; j < NUM_OF_CHECKBITS; j++)
		{
			for (k = checkbits[j]; k < checkbits[j + 1] - 1; k++)
			{
				/* Mapping from 63 bits to 57 bits of data */
				out_buff[offset_out + l] = in_buff[offset_in + k];
				l++;
			}
		}
		l = 0;
	}

	return corrected;
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
	FILE*				output_file = NULL;
	int					remote_port;
	struct sockaddr_in	remote_addr;
	char				recv_buff[RECV_BUFF_SIZE] = { 0 };
	char				write_buff[WRITE_BUFF_SIZE] = { 0 };
	char				mid_recv[MID_RECV_BUFF_SIZE] = { 0 };
	char				mid_write[MID_WRITE_BUFF_SIZE] = { 0 };
	int					bytes_rcvd = 0;
	int					bytes_wrtn = 0;
	int					to_recv = 0;
	int					to_write = RECV_BUFF_SIZE;
	int					bytes = 1;
	char				info_buff[MSG_SIZE] = { 0 };
	int					received = 0;
	int					wrote = 0;
	int					corrected = 0;

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

	/* Opening file for binary writing */
	output_file = fopen(argv[3], "wb");
	if (!output_file)
	{
		fprintf(stderr, "Error opening/creating file for binary write\n");
		closesocket(sock);
		WSACleanup();
		return -1;
	}

	while (1)
	{
		/* Receiving data from sender */
		to_recv = RECV_BUFF_SIZE;
		bytes_rcvd = 0;

		while (to_recv > 0 && bytes != 0)
		{
			bytes = recv(sock, recv_buff + bytes_rcvd, to_recv, 0);
			if (bytes == SOCKET_ERROR)
			{
				fprintf(stderr, "Error receiving data from sender: %ld\n", WSAGetLastError());
				fclose(output_file);
				closesocket(sock);
				WSACleanup();
				return -1;
			}
			to_recv -= bytes;
			bytes_rcvd+= bytes;
		}

		if (!bytes)
			break;

		received += bytes_rcvd;

		/* Decoding */
		bits_to_bytes(recv_buff, mid_recv, RECV_BUFF_SIZE);
		corrected += decode_hamming(mid_recv, mid_write, RECV_BUFF_SIZE, WRITE_BUFF_SIZE);
		bytes_to_bits(write_buff, mid_write, WRITE_BUFF_SIZE);

		/* Writing corrected data to file */
		to_write = WRITE_BUFF_SIZE;
		bytes_wrtn = 0;

		while (to_write > 0)
		{
			bytes = fwrite(write_buff + bytes_wrtn, 1, to_write, output_file);
			if (bytes < 0)
			{
				fprintf(stderr, "Error reading from file\n");
				fclose(output_file);
				closesocket(sock);
				WSACleanup();
				return -1;
			}
			to_write -= bytes;
			bytes_wrtn += bytes;
		}

		wrote += bytes_wrtn;
		
	}

	to_write = sprintf(info_buff, "%d:%d:%d:", received, wrote, corrected);
	bytes_wrtn = 0;

	/* Sending transmition and error correction info to sender */
	while ((bytes = send(sock, info_buff + bytes_wrtn, to_write, 0)) > 0)
	{
		bytes_wrtn += bytes;
		to_write -= bytes;
	}
	
	if (bytes == SOCKET_ERROR)
	{
		fprintf(stderr, "Error receiving info to print: %ld\n", WSAGetLastError());
		fclose(output_file);
		closesocket(sock);
		WSACleanup();
		return -1;
	}

	/* Sending FIN (both directions will be closed now) */
	status = shutdown(sock, SD_SEND);
	if (status == SOCKET_ERROR)
	{
		fprintf(stderr, "Error closing socket for sending: %ld\n", WSAGetLastError());
		fclose(output_file);
		closesocket(sock);
		WSACleanup();
		return -1;
	}

	fprintf(stderr, "received: %d bytes\n", received);
	fprintf(stderr, "wrote: %d bytes\n", wrote);
	fprintf(stderr, "corrected: %d errors\n", corrected);

	closesocket(sock);
	fclose(output_file);

	WSACleanup();

	return 0;

}