#include "ftl.h"

static void *recv_thread(void *data);

#define do_log(level, format, ...) \
	blog(level, "[rtmp stream: 'ftl'] " format, ##__VA_ARGS__)

#define warn(format, ...)  do_log(LOG_WARNING, format, ##__VA_ARGS__)
#define info(format, ...)  do_log(LOG_INFO,    format, ##__VA_ARGS__)
#define debug(format, ...) do_log(LOG_DEBUG,   format, ##__VA_ARGS__)

int _nack_init(ftl_t *ftl, enum obs_encoder_type type);
uint8_t* _nack_get_empty_packet(ftl_t *ftl, uint32_t ssrc, uint16_t sn, int *buf_len);
int _nack_send_packet(ftl_t *ftl, uint32_t ssrc, uint16_t sn, int len);
int nack_resend_packet(ftl_t *ftl, uint32_t ssrc, uint16_t sn);
struct media_component *_media_lookup(ftl_t *ftl, uint32_t ssrc);

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

	for (int i = 0; i < sizeof(ftl->media) / sizeof(ftl->media[0]); i++) {
		struct media_component *media = &ftl->media[i];
		media->nack_slots_initalized = false;
		media->seq_num = 0;
		media->payload_type = -1;
		
		if (OBS_ENCODER_AUDIO == i) {
			media->timestamp = 0;
			media->timestamp_step = 48000 / 50;
		}
		else if (OBS_ENCODER_VIDEO == i) {
			media->timestamp = 0;
			media->timestamp_step = 90000 / 30; //TODO: need to get actual frame rate
		}
	}

	return 0;
}

int FTL_set_ssrc(ftl_t *ftl, enum obs_encoder_type type, uint32_t ssrc) {
	
	int ret = 0;
	
	ftl->media[type].ssrc = ssrc;

	if (ftl->media[type].nack_slots_initalized == false) {
		ret = _nack_init(ftl, type);
		ftl->media[type].nack_slots_initalized = true;
	}

	return ret;
}

int FTL_set_ptype(ftl_t *ftl, enum obs_encoder_type type, uint8_t p_type) {
	ftl->media[type].payload_type = p_type;

	return 0;
}

int _make_video_rtp_packet(ftl_t *ftl, uint8_t *in, int in_len, uint8_t *out, int *out_len, int first_pkt) {
	uint8_t sbit, ebit;
	int frag_len;
	struct media_component *media = &ftl->media[OBS_ENCODER_VIDEO];

	sbit = first_pkt ? 1 : 0;
	ebit = (in_len + RTP_HEADER_BASE_LEN + RTP_FUA_HEADER_LEN) < ftl->max_mtu;

	uint32_t rtp_header;

	rtp_header = htonl((2 << 30) | (media->payload_type << 16) | media->seq_num);
	*((uint32_t*)out)++ = rtp_header;
	rtp_header = htonl(media->timestamp);
	*((uint32_t*)out)++ = rtp_header;
	rtp_header = htonl(media->ssrc);
	*((uint32_t*)out)++ = rtp_header;

	media->seq_num++;

	if (sbit && ebit) {
		sbit = ebit = 0;
		frag_len = in_len;
		*out_len = frag_len + RTP_HEADER_BASE_LEN;
		memcpy(out, in, frag_len);
	} else {

		if (sbit) {
			media->fua_nalu_type = in[0];
			in += 1;
			in_len--;
		}

		out[0] = media->fua_nalu_type & 0xE0 | 28;
		out[1] = (sbit << 7) | (ebit << 6) | (media->fua_nalu_type & 0x1F);

		out += 2;
		
		frag_len = ftl->max_mtu - RTP_HEADER_BASE_LEN - RTP_FUA_HEADER_LEN;

		if (frag_len > in_len) {
			frag_len = in_len;
		}

		memcpy(out, in, frag_len);

		*out_len = frag_len + RTP_HEADER_BASE_LEN + RTP_FUA_HEADER_LEN;
	}

	return frag_len + sbit;
}

int _make_audio_rtp_packet(ftl_t *ftl, uint8_t *in, int in_len, uint8_t *out, int *out_len) {
	int payload_len = in_len;

	uint32_t rtp_header;

	struct media_component *media = &ftl->media[OBS_ENCODER_AUDIO];

	rtp_header = htonl((2 << 30) | (1 << 23) | (media->payload_type << 16) | media->seq_num);
	*((uint32_t*)out)++ = rtp_header;
	rtp_header = htonl(media->timestamp);
	*((uint32_t*)out)++ = rtp_header;
	rtp_header = htonl(media->ssrc);
	*((uint32_t*)out)++ = rtp_header;

	media->seq_num++;
	media->timestamp += media->timestamp_step;

	memcpy(out, in, payload_len);

	*out_len = payload_len + RTP_HEADER_BASE_LEN;

	return in_len;
}

int _set_marker_bit(ftl_t *ftl, enum obs_encoder_type type, uint8_t *in) {
	uint32_t rtp_header;

	rtp_header = ntohl(*((uint32_t*)in));
	rtp_header |= 1 << 23; /*set marker bit*/
	*((uint32_t*)in) = htonl(rtp_header);
	
	ftl->media[type].timestamp += ftl->media[type].timestamp_step;

	return 0;
}

int FTL_sendPackets(ftl_t *ftl, struct encoder_packet *packet, int idx, bool is_header){

	if (packet->type == OBS_ENCODER_VIDEO) {
		int consumed = 0;
		int len = packet->size;

		unsigned char *p = packet->data;

		while (consumed < packet->size) {
			if (is_header) {
				if (consumed == 0) {
					p += 6; //first 6 bytes are some obs header with part of the sps
					consumed += 6;
				} else {
					p += 1; //another spacer byte of 0x1
					consumed += 1;
				}

				len = p[0] << 8 | p[1];
				p += 2;
				consumed += 2;
			}
			else {
				len = p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];

				if (len > (packet->size - consumed)) {
					warn("ERROR: got len of %d but packet only has %d left\n", len, (packet->size - consumed));
				}

				consumed += 4;
				p += 4;
			}

			//info("Got Video Packet of type %d size %d (%02X %02X %02X %02X %02X %02X %02X %02X)\n", p[0] & 0x1F, len, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);

			int pkt_len;
			int payload_size;

			int remaining = len;
			int first_fu = 1;

			while(remaining > 0) {

				uint16_t sn = ftl->media[OBS_ENCODER_VIDEO].seq_num;
				uint32_t ssrc = ftl->media[OBS_ENCODER_VIDEO].ssrc;
				uint8_t *pkt_buf;
				pkt_buf = _nack_get_empty_packet(ftl, ssrc, sn, &pkt_len);

				payload_size = _make_video_rtp_packet(ftl, p, remaining, pkt_buf, &pkt_len, first_fu);

				first_fu = 0;
				remaining -= payload_size;
				consumed += payload_size;
				p += payload_size;

				/*if all data has been consumed set marker bit*/
				if ((packet->size - consumed) == 0 && !is_header) {
					_set_marker_bit(ftl, OBS_ENCODER_VIDEO, pkt_buf);
				}

/*
				if (sendto(ftl->data_sock, ftl->pktBuf, pkt_len, 0, (struct sockaddr*) &ftl->server_addr, sizeof(ftl->server_addr)) == SOCKET_ERROR)
				{
					warn("sendto() failed with error code : %d", WSAGetLastError());
				}
*/
				_nack_send_packet(ftl, ssrc, sn, pkt_len);
			}
		}

	}
	else if(packet->type == OBS_ENCODER_AUDIO) {
		int pkt_len;
		uint16_t sn = ftl->media[OBS_ENCODER_VIDEO].seq_num;
		uint32_t ssrc = ftl->media[OBS_ENCODER_VIDEO].ssrc;
		uint8_t *pkt_buf;

		pkt_buf = _nack_get_empty_packet(ftl, ssrc, sn, &pkt_len);

		_make_audio_rtp_packet(ftl, packet->data, packet->size, pkt_buf, &pkt_len);

		_nack_send_packet(ftl, ssrc, sn, pkt_len);
/*
		if (sendto(ftl->data_sock, ftl->pktBuf, pkt_len, 0, (struct sockaddr*) &ftl->server_addr, sizeof(ftl->server_addr)) == SOCKET_ERROR)
		{
			warn("sendto() failed with error code : %d", WSAGetLastError());
		}
*/
	}
	else {
		warn("Got packet type %d\n", packet->type);
	}

	return 0;
}

int _nack_init(ftl_t *ftl, enum obs_encoder_type type) {

	struct media_component *media = &ftl->media[type];

	for (int i = 0; i < NACK_RB_SIZE; i++) {
		if ((media->nack_slots[i] = (struct nack_slot *)malloc(sizeof(struct nack_slot))) == NULL) {
			warn("Failed to allocate memory for nack buffer\n");
			return -1;
		}

		struct nack_slot *slot = media->nack_slots[i];

		if (pthread_mutex_init(&slot->mutex, NULL) != 0) {
			warn("Failed to allocate memory for nack buffer\n");
			return -1;
		}

		slot->len = 0;
		slot->sn = -1;
		slot->insert_ns = 0;
	}

	return 0;
}

struct media_component *_media_lookup(ftl_t *ftl, uint32_t ssrc) {
	struct media_component *media = NULL;

	for (int i = 0; i < sizeof(ftl->media) / sizeof(ftl->media[0]); i++) {
		if (ftl->media[i].ssrc == ssrc) {
			media = &ftl->media[i];
			break;
		}
	}

	return media;
}

uint8_t* _nack_get_empty_packet(ftl_t *ftl, uint32_t ssrc, uint16_t sn, int *buf_len) {
	struct media_component *media;

	if ( (media = _media_lookup(ftl, ssrc)) == NULL) {
		warn("Unable to find ssrc %d\n", ssrc);
		return NULL;
	}

	/*map sequence number to slot*/
	struct nack_slot *slot = media->nack_slots[sn % NACK_RB_SIZE];

	pthread_mutex_lock(&slot->mutex);

	*buf_len = sizeof(slot->packet);
	return slot->packet;	
}

int _nack_send_packet(ftl_t *ftl, uint32_t ssrc, uint16_t sn, int len) {
	struct media_component *media;
	int tx_len;

	if ((media = _media_lookup(ftl, ssrc)) == NULL) {
		warn("Unable to find ssrc %d\n", ssrc);
		return -1;
	}

	/*map sequence number to slot*/
	struct nack_slot *slot = media->nack_slots[sn % NACK_RB_SIZE];

	slot->len = len;
	slot->sn = sn;
	slot->insert_ns = os_gettime_ns();

	if ((tx_len = sendto(ftl->data_sock, slot->packet, slot->len, 0, (struct sockaddr*) &ftl->server_addr, sizeof(ftl->server_addr))) == SOCKET_ERROR)
	{
		warn("sendto() failed with error code : %d", WSAGetLastError());
	}

	pthread_mutex_unlock(&slot->mutex);

	return tx_len;
}

int nack_resend_packet(ftl_t *ftl, uint32_t ssrc, uint16_t sn) {
	struct media_component *media;
	int tx_len;

	if ((media = _media_lookup(ftl, ssrc)) == NULL) {
		warn("Unable to find ssrc %d\n", ssrc);
		return -1;
	}

	/*map sequence number to slot*/
	struct nack_slot *slot = media->nack_slots[sn % NACK_RB_SIZE];

	pthread_mutex_lock(&slot->mutex);

	if (slot->sn != sn) {
		warn("[%d] expected sn %d in slot but found %d...discarding retransmit request\n", ssrc, sn, slot->sn);
		pthread_mutex_unlock(&slot->mutex);
		return 0;
	}

	uint64_t req_delay = os_gettime_ns() - slot->insert_ns;

	tx_len = _nack_send_packet(ftl, ssrc, sn, slot->len);
	info("[%d] resent sn %d, request delay was %d ms\n", ssrc, sn, req_delay / 1000000);

	return tx_len;
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

	if ((buf = (unsigned char*)malloc(MAX_PACKET_MTU)) == NULL) {
		printf("Failed to allocate recv buffer\n");
		return NULL;
	}


	//os_set_thread_name("ftl-stream: recv_thread");

	while (1) {

#ifdef _WIN32
		ret = recv(ftl->data_sock, buf, MAX_PACKET_MTU, 0);
#else
		ret = recv(stream->sb_socket, buf, bytes, 0);
#endif
		if (ret <= 0) {
			continue;
		}

		info("Got recv of size %d bytes\n", ret);

		int version, padding, feedbackType, ptype, length, ssrcSender, ssrcMedia;
		uint16_t snBase, blp, sn;
		int recv_len = ret;

		if (recv_len < 2) {
			warn("recv packet too small to parse, discarding\n");
			continue;
		}

		/*extract rtp header*/
		version = (buf[0] >> 6) & 0x3;
		padding = (buf[0] >> 5) & 0x1;
		feedbackType = buf[0] & 0x1F;
		ptype = buf[1];

		if (feedbackType == 1 && ptype == 205) {
			info("Got NACK retransmit request\n");

			length = ntohs(*((uint16_t*)(buf + 2)));

			if (recv_len < ((length+1) * 4)) {
				warn("reported len was %d but packet is only %d...discarding\n", recv_len, ((length + 1) * 4));
				continue;
			}

			ssrcSender = ntohl(*((uint32_t*)(buf+4)));
			ssrcMedia = ntohl(*((uint32_t*)(buf+8)));

			uint16_t *p = (uint16_t *)(buf + 12);

			for (int fci = 0; fci < (length - 2); fci++) {
				//request the first sequence number
				snBase = ntohs(*p++);
				nack_resend_packet(ftl, ssrcMedia, snBase);
				blp = ntohs(*p++);
				if (blp) {
					for (int i = 0; i < 16; i++) {
						if ((blp & (1 << i)) != 0) {
							sn = snBase + i + 1;
							nack_resend_packet(ftl, ssrcMedia, sn);
						}
					}
				}
			}
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