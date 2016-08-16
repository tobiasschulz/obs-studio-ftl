#include "ftl.h"

static void *recv_thread(void *data);

#define do_log(level, format, ...) \
	blog(level, "[rtmp stream: 'ftl'] " format, ##__VA_ARGS__)

#define warn(format, ...)  do_log(LOG_WARNING, format, ##__VA_ARGS__)
#define info(format, ...)  do_log(LOG_INFO,    format, ##__VA_ARGS__)
#define debug(format, ...) do_log(LOG_DEBUG,   format, ##__VA_ARGS__)

int FTL_init_data(ftl_t *ftl, char *ingest) {
	WSADATA wsa;
	int ret;
	struct hostent *server;
	
	//Initialise winsock
	printf("Initialising Winsock...");
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
	{
		printf("Failed. Error Code : %d", WSAGetLastError());
		return EXIT_FAILURE;
	}
	printf("Initialised\n");

	//Create a socket
	if ((ftl->data_sock = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET)
	{
		printf("Could not create socket : %d", WSAGetLastError());
	}
	printf("Socket created.\n");

	if ( (server = gethostbyname(ingest)) == NULL ) {
		printf("ERROR, no such host as %s\n", ingest);
		return -1;
	}

	//Prepare the sockaddr_in structure
	ftl->server_addr.sin_family = AF_INET;
	memcpy((char *)&ftl->server_addr.sin_addr.s_addr, (char *)server->h_addr, server->h_length);
	ftl->server_addr.sin_port = htons(FTL_UDP_DATA_PORT);
/*
	if (bind(ftl->data_sock, (struct sockaddr *)&ftl->server_addr, sizeof(ftl->server_addr)) == SOCKET_ERROR)
	{
		printf("Bind failed with error code : %d", WSAGetLastError());
		exit(EXIT_FAILURE);
	}
	puts("Bind done");
	*/

	ret = pthread_create(&ftl->recv_thread, NULL, recv_thread, ftl);
	if (ret != 0) {
		//FTL_Close(ftl);
		//warn("Failed to create send thread");
		return -1;
	}

	ftl->max_mtu = 1392;
	if ((ftl->pktBuf = (uint8_t*)malloc(ftl->max_mtu)) == NULL) {
		warn("Failed to alloc memory for pkt buffer\n");
		return -1;
	}

	ftl->video_sn=0;
	ftl->video_ptype = 96;
	ftl->timestamp = 0;
	ftl->timestamp_step = 90000 / 30;
	ftl->video_ssrc = 0x12345678;

	return 0;
}

int _make_rtp_packet(ftl_t *ftl, uint8_t *in, int in_len, uint8_t *out, int *out_len, int first_pkt, int mbit) {
	uint8_t sbit, ebit;
	int frag_len;

	sbit = first_pkt ? 1 : 0;
	ebit = (in_len + RTP_HEADER_BASE_LEN + RTP_FUA_HEADER_LEN) < ftl->max_mtu;

	uint32_t rtp_header;

	rtp_header = htonl((2 << 30) | (mbit << 23) | (ftl->video_ptype << 16) | ftl->video_sn);
	*((uint32_t*)out)++ = rtp_header;
	rtp_header = htonl(ftl->timestamp);
	*((uint32_t*)out)++ = rtp_header;
	rtp_header = htonl(ftl->video_ssrc);
	*((uint32_t*)out)++ = rtp_header;

	ftl->video_sn++;
	if (mbit) {
		ftl->timestamp += ftl->timestamp_step;
	}

	out[0] = in[0] & 0xE0 | 28;
	out[1] = (sbit << 7) | (ebit << 6) | (in[0] & 0x1F);

	out += 2;

	frag_len = ftl->max_mtu - RTP_HEADER_BASE_LEN - RTP_FUA_HEADER_LEN;

	if (frag_len > in_len) {
		frag_len = in_len;
	}

	in += 1;

	memcpy(out, in, frag_len);

	*out_len = frag_len + RTP_HEADER_BASE_LEN + RTP_FUA_HEADER_LEN;

	return frag_len + 1;
}

int FTL_sendPackets(ftl_t *ftl, struct encoder_packet *packet, int idx){

	if (packet->type == OBS_ENCODER_VIDEO) {
		int consumed = 0;

		unsigned char *p = packet->data;

		while (consumed < packet->size) {
			int len = p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
			consumed += 4;
			p += 4;

			info("Got Video Packet of type %d size %d (%02X %02X %02X %02X %02X %02X %02X %02X)\n", p[0] & 0x1F, len, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);

			int pkt_len;
			uint8_t *tmp;
			int payload_size;

			tmp = p;
			int remaining = len;
			int first_fu = 1;

			while(remaining > 0) {
				int mbit = (packet->size - consumed + RTP_HEADER_BASE_LEN + RTP_FUA_HEADER_LEN) < ftl->max_mtu ? 1 : 0;
				payload_size = _make_rtp_packet(ftl, tmp, remaining, ftl->pktBuf, &pkt_len, first_fu, mbit);

				if (sendto(ftl->data_sock, ftl->pktBuf, pkt_len, 0, (struct sockaddr*) &ftl->server_addr, sizeof(ftl->server_addr)) == SOCKET_ERROR)
				{
					warn("sendto() failed with error code : %d", WSAGetLastError());
					exit(EXIT_FAILURE);
				}
				first_fu = 0;
				tmp += payload_size;
				remaining -= payload_size;

				p += payload_size;
				consumed += payload_size;
			}
		}

	}

	return 0;
}

#if 0
ret = pthread_create(&ftl->recv_thread, NULL, send_thread, stream);
if (ret != 0) {
	RTMP_Close(&stream->rtmp);
	warn("Failed to create send thread");
	return OBS_OUTPUT_ERROR;
}

os_atomic_set_bool(&stream->active, true);
while (next) {
	if (!send_meta_data(stream, idx++, &next)) {
		warn("Disconnected while attempting to connect to "
			"server.");
		return OBS_OUTPUT_DISCONNECTED;
	}
}
obs_output_begin_data_capture(stream->output, 0);

return OBS_OUTPUT_SUCCESS;
#endif
int FTL_LogSetCallback() {
}

static void *recv_thread(void *data)
{
	ftl_t *ftl = (ftl_t *)data;
	int ret;
	unsigned char *buf;

	if ((buf = (unsigned char*)malloc(BUFLEN)) == NULL) {
		printf("Failed to allocate recv buffer\n");
		return NULL;
	}


	//os_set_thread_name("ftl-stream: recv_thread");

	while (1) {

#ifdef _WIN32
		ret = recv(ftl->data_sock, buf, BUFLEN, 0);
#else
		ret = recv(stream->sb_socket, buf, bytes, 0);
#endif

		if (ret > 0) {
			printf("Got recv of size %d bytes\n", ret);
		}
	}


#if 0

	while (os_sem_wait(stream->send_sem) == 0) {
		struct encoder_packet packet;

		if (stopping(stream) && stream->stop_ts == 0) {
			break;
		}

		if (!get_next_packet(stream, &packet))
			continue;

		if (stopping(stream)) {
			if (packet.sys_dts_usec >= (int64_t)stream->stop_ts) {
				obs_free_encoder_packet(&packet);
				break;
			}
		}

		/*
		if (!stream->sent_headers) {
		if (!send_headers(stream)) {
		os_atomic_set_bool(&stream->disconnected, true);
		break;
		}
		}
		*/

		if (send_packet(stream, &packet, false, packet.track_idx) < 0) {
			os_atomic_set_bool(&stream->disconnected, true);
			break;
		}
	}

	if (disconnected(stream)) {
		info("Disconnected from %s", stream->path.array);
	}
	else {
		info("User stopped the stream");
	}

	//RTMP_Close(&stream->rtmp);

	if (!stopping(stream)) {
		pthread_detach(stream->send_thread);
		obs_output_signal_stop(stream->output, OBS_OUTPUT_DISCONNECTED);
	}
	else {
		obs_output_end_data_capture(stream->output);
	}

	free_packets(stream);
	os_event_reset(stream->stop_event);
	os_atomic_set_bool(&stream->active, false);
	stream->sent_headers = false;
	return NULL;

#endif
}