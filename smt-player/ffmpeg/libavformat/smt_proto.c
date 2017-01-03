#include "smt_proto.h"
#ifdef SMT_PROTOCAL_SIGNAL
#include "smt_signal.h"
#endif
#include "avformat.h"
#include "libavutil/avassert.h"
#include "libavutil/time.h"

//#define SMT_DUMP

#if 0
/*for receive*/
static bool             stack_init = false;
static smt_status       packet_parser_status;
static smt_parse_phase  packet_parse_phase;
static unsigned char*   packet_buffer = NULL;
static unsigned int     packet_buffer_data_len = 0;
static int              process_position;
static smt_packet*      mpu_head[MAX_ASSET_NUMBER];
static unsigned int     need_more_data = 0, has_more_data = 0;
static smt_packet*      current_packet;
static int				packet_counter;

/*for send*/
static int				asset;
static int				pkt_counter = 0;
static int 				mpu_seq[MAX_ASSET_NUMBER] = {0};
static int              pkt_seq[MAX_ASSET_NUMBER] = {0};
static int				moof_index;
static int				sample_index;
#endif

#define INPUT_URL_NUM_MAX 100
extern smt_callback     smt_callback_entity;
//for ffmpeg
int64_t ffmpeg_begin_time = 0;
int64_t diff_time = 0;
//for ffplay
#if defined(__ANDROID__)
int64_t begin_time_value = 0;
#endif
/*for send*/
static int				asset;
static int				pkt_counter = 0;
static int 				mpu_seq[MAX_ASSET_NUMBER] = {0};
static int              pkt_seq[MAX_ASSET_NUMBER] = {0};
static int				moof_index;
static int				sample_index;
int g_id_old=-1;
int g_mpu_old=0;
enum {
    INVALID_MPU  = -1,
    INVALID_DATA = -2,
    OUT_OF_RANGE = -3,
    INVALID_MFU  = -4,
    INVALID_SAMPLE_NUMBER = -5,
    OUT_OF_MEMORY= -6,
    INVALID_SIGNALLING_MESSAGE  = -7,
};


//add for test
static void print_all(smt_payload_sig * sig){
        printf("--------------------------lalalalalal----------------------------------------------------\n");
        printf("f_i=%d\nres=%u\nH=%d\nA=%d\nfrag_counter=%u\nMSG_length=%d\ndata_len=%u\n",
                sig->f_i,sig->res,sig->H,sig->A,sig->frag_counter,sig->MSG_length,sig->data_len);
        int i = 0;
        printf("data=\n");
        for(i = 0; i < sig->MSG_length; ++i){
            printf("%u",sig->data[i]);
        }
        printf("\n");
        printf("--------------------------lalalallala----------------------------------------------------\n");
        fflush(stdout);
}
//add for test

/*
 *  output: p_zero_oclock,  time of zero oclock (local time zone) in time_t format
 *  return: int64_t formate 
*/
static int64_t get_today_zero_oclock(time_t *p_zero_oclock) {
    time_t  now_time_second;
    int64_t now_time_micros =  av_gettime();
    struct tm today_zero_time;
    time_t time_zero_second;
    int64_t time_zero_micros;

    now_time_second = (time_t)(now_time_micros/ (1000*1000));
    today_zero_time = *localtime(&now_time_second);
    today_zero_time.tm_hour = 0;
    today_zero_time.tm_min  = 0;
    today_zero_time.tm_sec  = 0;
    time_zero_second = mktime(&today_zero_time);
    if(p_zero_oclock != NULL) {
        *p_zero_oclock = time_zero_second;
    }
    time_zero_micros = now_time_micros - 
                               ((int64_t)(now_time_second - time_zero_second ))*1000*1000 - 
                               (now_time_micros%(1000*1000));
    return time_zero_micros;
}





static smt_status smt_parse_mpu_payload(URLContext *h, smt_receive_entity *recv, smt_payload_mpu **p)
{
    unsigned char* buffer = recv->packet_buffer + recv->process_position;
    unsigned int data_len = 0, aggregation_du_index = 0;
    unsigned int remained_len = 0;
    unsigned int offset = 0;
    smt_payload_mpu *payload = *p;
    if(recv->packet_parse_phase == SMT_PARSE_PAYLOAD_HEADER){
        if(recv->packet_buffer_data_len - recv->process_position < SMT_MPU_PAYLOAD_HEAD_LENGTH){
            recv->packet_parser_status = SMT_STATUS_NEED_MORE_DATA;
            recv->need_more_data = SMT_MPU_PAYLOAD_HEAD_LENGTH - (recv->packet_buffer_data_len - recv->process_position);
            return SMT_STATUS_NEED_MORE_DATA;
        }
        payload->length = buffer[0] << 8 | buffer[1];
        switch((buffer[2] >> 4) & 0x0f){
            case 0x00:
                payload->FT = mpu_metadata;
                break;
            case 0x01:
                payload->FT = movie_fragment_metadata;
                break;
            case 0x02:
                payload->FT = mfu;
                break;
            default:
                recv->packet_parser_status = SMT_STATUS_INIT;
                return SMT_STATUS_NOT_SUPPORT;
        }
        payload->T = (buffer[2] >> 3) & 0x01;
        switch((buffer[2] >> 1) & 0x03){
            case 0x00:
                payload->f_i = complete_data;
                break;
            case 0x01:
                payload->f_i = first_fragment;
                break;
            case 0x02:
                payload->f_i = middle_fragment;
                break;
            case 0x03:
                payload->f_i = last_fragment;
                break;
        }
        payload->A = buffer[2] & 0x01;
        payload->frag_counter = buffer[3];
        payload->MPU_sequence_number = (buffer[4] << 24) | (buffer[5] << 16) | (buffer[6] << 8) | buffer[7];
		recv->process_position += SMT_MPU_PAYLOAD_HEAD_LENGTH;
		offset = SMT_MPU_PAYLOAD_HEAD_LENGTH;
        recv->packet_parse_phase = SMT_ALLOC_PAYLOAD_DATA;
    }

    if(recv->packet_parse_phase == SMT_ALLOC_PAYLOAD_DATA){
        if(recv->packet_buffer_data_len - recv->process_position < (payload->length - SMT_MPU_PAYLOAD_HEAD_LENGTH + 2)){
            recv->packet_parser_status = SMT_STATUS_NEED_MORE_DATA;
            recv->need_more_data = (payload->length - SMT_MPU_PAYLOAD_HEAD_LENGTH + 2) - (recv->packet_buffer_data_len - recv->process_position);
            return SMT_STATUS_NEED_MORE_DATA;
        }
        if(payload->A){
            data_len = payload->length - SMT_MPU_PAYLOAD_HEAD_LENGTH + 2;
            payload->data = (unsigned char*)av_mallocz(data_len);
            while(data_len > 0){
                payload->DU_length[aggregation_du_index] = ((buffer+offset)[0] << 8) | (buffer+offset)[1];
                offset += 2;
				recv->process_position += 2;
                if(payload->FT == mfu){
                    payload->DU_Header[aggregation_du_index].movie_fragment_sequence_number = ((buffer+offset)[0] << 24) | ((buffer+offset)[1] << 16) | ((buffer+offset)[2] << 8) | (buffer+offset)[3];
                    payload->DU_Header[aggregation_du_index].sample_number = ((buffer+offset)[4] << 24) | ((buffer+offset)[5] << 16) | ((buffer+offset)[6] << 8) | (buffer+offset)[7];
                    payload->DU_Header[aggregation_du_index].offset = ((buffer+offset)[8] << 24) | ((buffer+offset)[9] << 16) | ((buffer+offset)[10] << 8) | (buffer+offset)[11];
                    payload->DU_Header[aggregation_du_index].priority = (buffer+offset)[12];
                    payload->DU_Header[aggregation_du_index].dep_counter = (buffer+offset)[13];
                    offset += 14;
					recv->process_position += 14;
                }
                memcpy(payload->data + payload->data_len, buffer + offset, payload->DU_length[aggregation_du_index]);
                payload->data_len += payload->DU_length[aggregation_du_index];
                offset += payload->DU_length[aggregation_du_index];
                recv->process_position += payload->DU_length[aggregation_du_index];
                aggregation_du_index++;
                if(aggregation_du_index > MAX_AGGGREGATION_DU_NUMBER){
                    av_log(h, AV_LOG_ERROR, "aggregated payload number is more than MAX number %d!\n", MAX_AGGGREGATION_DU_NUMBER);
                    return SMT_STATUS_NOT_SUPPORT;
                }
                data_len -= (20 + payload->DU_length[aggregation_du_index]);         
            }
        }else{
            if(payload->FT == mfu){
                payload->DU_Header[aggregation_du_index].movie_fragment_sequence_number = ((buffer+offset)[0] << 24) | ((buffer+offset)[1] << 16) | ((buffer+offset)[2] << 8) | (buffer+offset)[3];
                payload->DU_Header[aggregation_du_index].sample_number = ((buffer+offset)[4] << 24) | ((buffer+offset)[5] << 16) | ((buffer+offset)[6] << 8) | (buffer+offset)[7];
                payload->DU_Header[aggregation_du_index].offset = ((buffer+offset)[8] << 24) | ((buffer+offset)[9] << 16) | ((buffer+offset)[10] << 8) | (buffer+offset)[11];
                payload->DU_Header[aggregation_du_index].priority = (buffer+offset)[12];
                payload->DU_Header[aggregation_du_index].dep_counter = (buffer+offset)[13];
                offset += 14;
				recv->process_position += 14;
				data_len = payload->length - (SMT_MPU_PAYLOAD_HEAD_LENGTH + SMT_MPU_PAYLOAD_DU_HEAD_LENGTH) + 2;
            }else
            	data_len = payload->length - SMT_MPU_PAYLOAD_HEAD_LENGTH + 2;
            
            payload->data = (unsigned char*)av_mallocz(data_len);
            memcpy(payload->data, buffer + offset, data_len);
            offset += data_len;
            payload->data_len = data_len;
            av_assert0(payload->data_len);
            recv->process_position += data_len;
        }
    }
    //av_log(h, AV_LOG_WARNING, "pld: mpu number: %d, fragment type = %d, f_i = %d \n", payload->MPU_sequence_number, payload->FT, payload->f_i);
    return SMT_STATUS_OK;
}

static smt_status smt_parse_gfd_payload(URLContext *h, smt_receive_entity *recv, smt_payload_gfd **p)
{
    unsigned char* buffer = recv->packet_buffer + recv->process_position;
    unsigned int size = recv->packet_buffer_data_len - recv->process_position;
    smt_payload_gfd *payload = *p;
    if(recv->packet_parse_phase == SMT_PARSE_PAYLOAD_HEADER){
        if(size < SMT_GFD_PAYLOAD_HEAD_LENGTH){
            recv->packet_parser_status = SMT_STATUS_NEED_MORE_DATA;
            recv->need_more_data = SMT_GFD_PAYLOAD_HEAD_LENGTH - size;
            return SMT_STATUS_NEED_MORE_DATA;
        }
        payload->C = (buffer[0] >> 7) & 0x01;
        payload->L = (buffer[0] >> 6) & 0x01;
        payload->B = (buffer[0] >> 5) & 0x01;
        payload->CP = ((buffer[0] & 0x1f) << 3) | ((buffer[1] >> 5) & 0x0f);
        payload->RES = buffer[1] & 0x1f;
        payload->TOI = (buffer[2] << 24) | (buffer[3] << 16) | (buffer[4] << 8) | buffer[5];
        payload->start_offset = (buffer[6] << 40) | (buffer[7] << 32) | (buffer[8] << 24) | (buffer[9] << 16) | (buffer[10] << 8) | buffer[11];
        recv->process_position += SMT_PARSE_PAYLOAD_HEADER;
        recv->packet_parse_phase = SMT_ALLOC_PAYLOAD_DATA;
    }
    if(recv->packet_parse_phase == SMT_ALLOC_PAYLOAD_DATA){
        payload->data = (unsigned char*)av_mallocz(size - SMT_PARSE_PAYLOAD_HEADER); // smt protocol has some problem for GFD mode. If length of sending packet is more than MTU, we have no way to get real length of payload data.
        memcpy(payload->data, buffer + SMT_PARSE_PAYLOAD_HEADER, size - SMT_PARSE_PAYLOAD_HEADER);
        recv->process_position += (size - SMT_PARSE_PAYLOAD_HEADER);
    }
    return SMT_STATUS_OK;
}

static smt_status smt_parse_sig_payload(URLContext *h, smt_receive_entity *recv, smt_payload_sig **p)
{
    unsigned char* buffer = recv->packet_buffer + recv->process_position; 
    int size = recv->packet_buffer_data_len - recv->process_position;
    unsigned int   offset = 0, data_len = 0;
    smt_payload_sig *payload = *p;

    if(recv->packet_parse_phase == SMT_PARSE_PAYLOAD_HEADER){
        if(size < SMT_GFD_PAYLOAD_HEAD_LENGTH){
            recv->packet_parser_status = SMT_STATUS_NEED_MORE_DATA;
            recv->need_more_data = SMT_GFD_PAYLOAD_HEAD_LENGTH - size;
            return SMT_STATUS_NEED_MORE_DATA;
        }
        // printf("~~~~~~~sig~~~~~~~~~~\n");
        // int i = 0;
        // for(i = 0; i < 4; ++i){
        //     printf("%u\n",buffer[i]);

        // }
        // printf("~~~~~~~sig~~~~~~~~~~\n");
        switch((buffer[0] >> 6) & 0x03){
            case 0x00:
                payload->f_i = complete_data;
                break;
            case 0x01:
                payload->f_i = first_fragment;
                break;
            case 0x02:
                payload->f_i = middle_fragment;
                break;
            case 0x03:
                payload->f_i = last_fragment;
                break;
        }
        payload->res = (buffer[0] >> 2) & 0x0f;
        payload->H = (buffer[0] >> 1) & 0x01;
        payload->A = buffer[0] & 0x01;
        payload->frag_counter = buffer[1];
        if(payload->A && payload->frag_counter){
            av_log(h, AV_LOG_ERROR, "aggregated payload can not count fragments!");
            recv->packet_parser_status = SMT_STATUS_INIT;
            return SMT_STATUS_ERROR;
        }
		recv->process_position += SMT_SIG_PAYLOAD_HEAD_LENGTH;
		offset = SMT_SIG_PAYLOAD_HEAD_LENGTH;
        recv->packet_parse_phase = SMT_ALLOC_PAYLOAD_DATA;
    }
    
    if(recv->packet_parse_phase == SMT_ALLOC_PAYLOAD_DATA){
        while(offset < size){
                if(payload->A){
                    if(payload->H){
                        payload->MSG_length = (buffer[2] << 8) | buffer[3] |buffer[4]|buffer[5];
                        recv->process_position += 4;
                        offset += 4;
                    }else{
                        payload->MSG_length =(buffer[2] << 8) | buffer[3];
                        recv->process_position += 2;
                        offset += 2;
                        offset += 1;
                    }
                    data_len += payload->MSG_length;
                    payload->data = (unsigned char*)av_realloc(payload->data, data_len);
                    if(!payload->data){
                        av_log(h, AV_LOG_FATAL, "can not realloc memory for signaling message!");
                        recv->packet_parser_status = SMT_STATUS_INIT;
                        return SMT_STATUS_ERROR; 
                    }
                    memset(payload->data, 0, data_len);
                    memcpy(payload->data, buffer + offset, data_len);
					recv->process_position += data_len;
                    offset += data_len;
                }else{
                     //add for test
                    if(payload->H){
                        payload->MSG_length = (buffer[2] << 8) | buffer[3] |buffer[4]|buffer[5];
                        recv->process_position += 4;
                        offset += 4;
                    }else{
                        payload->MSG_length =(buffer[2] << 8) | buffer[3];
                        recv->process_position += 2;
                        offset += 2;
                    }
                    //add for test
                    payload->data_len = size - offset;
                    payload->data = (unsigned char*)av_mallocz(size - offset);
                    memset(payload->data, 0, payload->data_len);
                    memcpy(payload->data, buffer+offset, payload->data_len);

                    recv->process_position += payload->data_len;
                    offset += payload->data_len;
                }
            }


    }
    
    return SMT_STATUS_OK;
}

static smt_status smt_parse_repair_payload(URLContext *h, smt_receive_entity *recv, smt_payload_id **p)
{
    unsigned char* buffer = recv->packet_buffer + recv->process_position;
    int size = recv->packet_buffer_data_len - recv->process_position;
    unsigned int   offset = 0;
    smt_payload_id *payload = *p;
    if(recv->packet_parse_phase == SMT_PARSE_PAYLOAD_HEADER){
        if(size < SMT_ID_PAYLOAD_HEAD_LENGTH){
            recv->packet_parser_status = SMT_STATUS_NEED_MORE_DATA;
            recv->need_more_data = SMT_ID_PAYLOAD_HEAD_LENGTH - size;
            return SMT_STATUS_NEED_MORE_DATA;
        }
        payload->SS_start = (buffer[0] << 24) | (buffer[1] << 16) | (buffer[2] << 8) | buffer[3];
        payload->RSB_length = (buffer[4] << 16) | (buffer[5] << 8) | buffer[6];
        payload->RS_ID = (buffer[7] << 16) | (buffer[8] << 8) | buffer[9];
        payload->SSB_length = (buffer[10] << 16) | (buffer[11] << 8) | buffer[12];
		recv->process_position += SMT_ID_PAYLOAD_HEAD_LENGTH;
		offset = SMT_ID_PAYLOAD_HEAD_LENGTH;
        recv->packet_parse_phase = SMT_ALLOC_PAYLOAD_DATA;
    }

    if(recv->packet_parse_phase == SMT_ALLOC_PAYLOAD_DATA){
        if(size < MTU + 4){
            recv->packet_parser_status = SMT_STATUS_NEED_MORE_DATA;
            recv->need_more_data = MTU + 4 - size;
            return SMT_STATUS_NEED_MORE_DATA;
        }
        payload->data = (unsigned char *)av_mallocz(MTU);
        memcpy(payload->data, buffer + offset, MTU);
		recv->process_position += MTU;
        offset += MTU;
        payload->FFSRP_TS.TS_Indicator = ((buffer + offset)[0] >> 7) & 0x01; 
        payload->FFSRP_TS.FP_TS = (((buffer + offset)[0] << 24) & 0x7F) | ((buffer + offset)[1] << 16) | ((buffer + offset)[2] << 8) | (buffer + offset)[3];
        offset += 4;
        recv->process_position += 4;

    }

    return SMT_STATUS_OK;
}


static smt_status smt_parse_packet(URLContext *h, smt_receive_entity *recv, unsigned char* buffer, int size, smt_packet *p)
{
    smt_status ret = SMT_STATUS_OK;
    smt_payload *payload;
    bool    loop_end = false;
    if(!buffer || !size){
        av_log(h, AV_LOG_ERROR, "input error. buffer = %u,size = %d!\n",buffer, size);
        return SMT_STATUS_ERROR;
    }
	
    do{
        switch(recv->packet_parser_status){
            case SMT_STATUS_INIT:{
                recv->packet_buffer = (unsigned char *)av_mallocz(size);
                memcpy(recv->packet_buffer, buffer, size);
                recv->packet_buffer_data_len = size;
                if(recv->packet_buffer_data_len < SMT_PACKET_HEAD_LENGTH){
                    recv->packet_parser_status = SMT_STATUS_NEED_MORE_DATA;
                    recv->need_more_data = SMT_PACKET_HEAD_LENGTH - recv->packet_buffer_data_len;
                    return SMT_STATUS_NEED_MORE_DATA;
                }else{
                    recv->packet_parse_phase = SMT_PARSE_PACKET_HEADER;
                    recv->packet_parser_status = SMT_STATUS_OK;
                }
                break;
            }
            case SMT_STATUS_NEED_MORE_DATA:{
                recv->packet_buffer = (unsigned char *)av_realloc(recv->packet_buffer, recv->packet_buffer_data_len + size);
                memcpy(recv->packet_buffer+recv->packet_buffer_data_len, buffer, size);
                recv->packet_buffer_data_len += size;
                if(size < recv->need_more_data){
                    recv->need_more_data = recv->need_more_data - size;
                    return SMT_STATUS_NEED_MORE_DATA;
                }else
                    recv->packet_parser_status = SMT_STATUS_OK;
                break;
            }
            case SMT_STATUS_HAS_MORE_DATA:{
                recv->packet_buffer = (unsigned char *)av_realloc(recv->packet_buffer, recv->packet_buffer_data_len + size);
                memcpy(recv->packet_buffer+recv->packet_buffer_data_len, buffer, size);
                recv->packet_buffer_data_len += size;
                if(recv->packet_buffer_data_len < SMT_PACKET_HEAD_LENGTH){
                    recv->packet_parser_status = SMT_STATUS_NEED_MORE_DATA;
                    recv->need_more_data = SMT_PACKET_HEAD_LENGTH - recv->packet_buffer_data_len;
                    return SMT_STATUS_NEED_MORE_DATA;
                }else{
                    recv->packet_parse_phase = SMT_PARSE_PACKET_HEADER;
                    recv->packet_parser_status = SMT_STATUS_OK;
                }
                break;
            }
            case SMT_STATUS_OK:{
                if(recv->packet_parse_phase == SMT_PARSE_PACKET_HEADER){
                    p->V = (recv->packet_buffer[0] >> 6) & 0x03;
                    if(p->V != 0){
                        av_log(h, AV_LOG_ERROR, "only smt verion 0 is supported! current version: %d\n",p->V);
                        return SMT_STATUS_ERROR;
                    }
                    
                    p->C = (recv->packet_buffer[0] >> 5) & 0x01;
                    switch((recv->packet_buffer[0] >> 3) & 0x03){
                        case 0x00: 
                            p->FEC = no_fec; 
                            break;
                        case 0x01: 
                            p->FEC = al_fec_source; 
                            break;
                        case 0x02: 
                            p->FEC = al_fec_repair; 
                            break;
                        case 0x03: 
                            p->FEC = reserved; 
                            recv->packet_parser_status = SMT_STATUS_INIT;
                            av_log(h, AV_LOG_ERROR, "reserved FEC flag is not supported now.");
                            return SMT_STATUS_NOT_SUPPORT;
                    }
                    p->r = (recv->packet_buffer[0] >> 2) & 0x01;
                    p->X = (recv->packet_buffer[0] >> 1) & 0x01;
                    p->R = recv->packet_buffer[0] & 0x01;
                    if(p->R){
                        av_log(h, AV_LOG_WARNING, "Random access is not supported!");
                        recv->packet_parser_status = SMT_STATUS_INIT;
                        return SMT_STATUS_NOT_SUPPORT;
                    }
                    p->RES = (recv->packet_buffer[1] >> 6) & 0x03;
                    switch(recv->packet_buffer[1] & 0x3f){
                        case  0x00:
                            p->type = mpu_payload;
                            break;
                        case 0x01:
                            p->type = gfd_payload;
                            break;
                        case 0x02:
                            p->type = sig_payload;
                            break;
                        case 0x03:
                            p->type = repair_symbol_payload;
                            break;
                        default:
                            recv->packet_parser_status = SMT_STATUS_INIT;
                            av_log(h, AV_LOG_ERROR, "wrong packet type. type = %d\n",recv->packet_buffer[1] & 0x3f);
                            return SMT_STATUS_NOT_SUPPORT;
                    }
                    
                    p->packet_id = (recv->packet_buffer[2] << 8) | recv->packet_buffer[3];
                    p->timestamp = (recv->packet_buffer[4] << 24) | (recv->packet_buffer[5] << 16) | (recv->packet_buffer[6] << 8) | recv->packet_buffer[7];
                    p->packet_sequence_number = (recv->packet_buffer[8] << 24) | (recv->packet_buffer[9] << 16) | (recv->packet_buffer[10] << 8) | recv->packet_buffer[11];
                    p->packet_counter = (recv->packet_buffer[12] << 24) | (recv->packet_buffer[13] << 16) | (recv->packet_buffer[14] << 8) | recv->packet_buffer[15];
#if !defined(__ANDROID__)

                    if(smt_callback_entity.get_last_packet_counter(h) + 1 == p->packet_counter || smt_callback_entity.get_last_packet_counter(h) == 0) {
                    } else {
                        char* device = NULL;
                        device = get_av_log_device_info();
                        av_log_ext(NULL, AV_LOG_ERROR, "{\"device\":\"%s\",\"filename\":\"%s\",\"packet_lost\":\"%d\",\"packet_counter\":\"%u\",\"last_packet_counter\":\"%u\",\"time\":\"%lld\"}\n", 
                                device,
                                h->filename,
                                p->packet_counter - smt_callback_entity.get_last_packet_counter(h) -1,
                                p->packet_counter,
                                smt_callback_entity.get_last_packet_counter(h),
                                av_gettime());

                    }
                    smt_callback_entity.set_last_packet_counter(h, p->packet_counter);
#endif                    
                    recv->process_position += 16;
                    recv->packet_parse_phase = SMT_PARSE_PACKET_HEADER_EXTENSION;
                }
                
                if(recv->packet_parse_phase == SMT_PARSE_PACKET_HEADER_EXTENSION){
                    if(p->X){
                        if(recv->packet_buffer_data_len - recv->process_position < SMT_PACKET_HEAD_EXTENSION_LENGTH){
                            recv->packet_parser_status = SMT_STATUS_NEED_MORE_DATA;
                            recv->need_more_data = SMT_PACKET_HEAD_EXTENSION_LENGTH - (recv->packet_buffer_data_len - recv->process_position);
                            return SMT_STATUS_NEED_MORE_DATA;
                        }
                        if(!p->header_extension.type && !p->header_extension.length){
                            p->header_extension.type = (recv->packet_buffer[16] << 8) | recv->packet_buffer[17];
                            p->header_extension.length = (recv->packet_buffer[18] << 8) | recv->packet_buffer[19];
                            recv->process_position += 4;
                        }
        
                        if(recv->packet_buffer_data_len - recv->process_position < p->header_extension.length){
                            recv->packet_parser_status = SMT_STATUS_NEED_MORE_DATA;
                            recv->need_more_data = p->header_extension.length - (recv->packet_buffer_data_len - recv->process_position);
                            return SMT_STATUS_NEED_MORE_DATA;
                        }
                        
                        recv->process_position += p->header_extension.length; //skip this field
                    }
                    recv->packet_parse_phase = SMT_PARSE_PAYLOAD_HEADER;
                }
        
        
                payload = &(p->payload);
                switch(p->type){
                    case mpu_payload:{
                        smt_payload_mpu *mpu = (smt_payload_mpu *)payload;
                        ret = smt_parse_mpu_payload(h, recv, &mpu);
                        break;
                    }case gfd_payload:{
                        smt_payload_gfd *gfd = (smt_payload_gfd *)payload;
                        ret = smt_parse_gfd_payload(h, recv, &gfd);
                        break;
                    }case sig_payload:{
                        smt_payload_sig *sig = (smt_payload_sig *)payload;
                        ret = smt_parse_sig_payload(h, recv, &sig);
                        //begin:add for test
                        //print_all(sig);
                        //end:add for test
                        break;
                    }case repair_symbol_payload:{
                        smt_payload_id *id = (smt_payload_id *)payload;
                        ret = smt_parse_repair_payload(h, recv, &id);
                        break;
                    }
                }
        
                if(ret == SMT_STATUS_OK){
                    if(p->FEC == al_fec_source && (p->type != repair_symbol_payload))
                        recv->packet_parse_phase = SMT_PARSE_FEC_TAIL;
                }
        
                if(recv->packet_parse_phase == SMT_PARSE_FEC_TAIL){
                    if(recv->packet_buffer_data_len - recv->process_position <  SMT_SOURCE_FEC_PAYLOAD_ID_LENGTH){
                        recv->packet_parser_status = SMT_STATUS_NEED_MORE_DATA;
                        recv->need_more_data = SMT_SOURCE_FEC_PAYLOAD_ID_LENGTH - (recv->packet_buffer_data_len - recv->process_position);
                        return SMT_STATUS_NEED_MORE_DATA;
                    }
                    p->Source_FEC_payload_ID.SS_ID = ((recv->packet_buffer + recv->process_position)[0] << 24) | ((recv->packet_buffer + recv->process_position)[1] << 16) | ((recv->packet_buffer + recv->process_position)[2] << 8) | (recv->packet_buffer + recv->process_position)[3];
                    p->Source_FEC_payload_ID.FFSRP_TS = ((recv->packet_buffer + recv->process_position)[4] << 24) | ((recv->packet_buffer + recv->process_position)[5] << 16) | ((recv->packet_buffer + recv->process_position)[6] << 8) | (recv->packet_buffer + recv->process_position)[7];
                    recv->process_position += 8;
                }
            loop_end = true;
            break;
            }
        }

    }while(!loop_end);


	if(ret == SMT_STATUS_OK){
		av_assert0(recv->process_position <= recv->packet_buffer_data_len);
#ifdef FIXED_UDP_LEN
        if(recv->process_position < recv->packet_buffer_data_len)
            while(!*(recv->packet_buffer+recv->process_position)){
                recv->process_position++;
                if(recv->process_position == recv->packet_buffer_data_len)
                    break;
            }
#endif
		recv->has_more_data = recv->packet_buffer_data_len - recv->process_position;
		if(recv->has_more_data){
			recv->packet_parser_status = SMT_STATUS_HAS_MORE_DATA;
			memmove(recv->packet_buffer, recv->packet_buffer+recv->process_position, recv->has_more_data);
			recv->packet_buffer = (unsigned char *)av_realloc(recv->packet_buffer, recv->has_more_data);
			return SMT_STATUS_HAS_MORE_DATA;
		}else{
			recv->packet_parse_phase = SMT_PARSE_NOT_START;
			recv->packet_parser_status = SMT_STATUS_INIT;
			av_freep(&recv->packet_buffer);
		}
	}else if(ret == SMT_STATUS_NEED_MORE_DATA){
		recv->has_more_data = 0;
	}
/*
    av_log(h, AV_LOG_WARNING, "pkt: sequence number: %d, asset id = %d, counter = %d, size = %d, timestamp = %d \n",
        p->packet_sequence_number,
        p->packet_id,
        p->packet_counter,
        size,
        p->timestamp);
*/
    return ret;
}

static int smt_find_field(char *buf, int buf_len, char *field, int field_len)
{
	int pos = 0;
	if(buf_len < field_len)
		return -1;
	while(pos <= buf_len - field_len){
		if(!strncmp(buf+pos, field, field_len))
			return pos;
		pos++;
	}

	return -1;
}




static int smt_parse_media_info(smt_mpu *mpu) {
    if(!mpu) return 0;
    unsigned char* mp4buffer = mpu->mpu_header_data;
    unsigned int   length    = mpu->mpu_header_length;
    int mdhd_offset = smt_find_field(mp4buffer ,length,"mdhd", 4);
    if(mdhd_offset  >= 0) {
        unsigned char* mdhd = mp4buffer  + mdhd_offset;
        int mdhd_timescale_offset = 0;
        int mdhd_duration_offset = 0;
        int version = (mdhd+0x04)[0];
        int64_t duration = 0;
        int32_t timescale = 0;


        if(version) {
            mdhd_timescale_offset = 0x18;
            mpu->media_duration = mdhd[mdhd_timescale_offset + 0x04 + 0] << 56 |
                       mdhd[mdhd_timescale_offset + 0x04 + 1] << 48 |
                       mdhd[mdhd_timescale_offset + 0x04 + 2] << 40 |
                       mdhd[mdhd_timescale_offset + 0x04 + 3] << 32 |
                       mdhd[mdhd_timescale_offset + 0x04 + 4] << 24 |
                       mdhd[mdhd_timescale_offset + 0x04 + 5] << 16 |
                       mdhd[mdhd_timescale_offset + 0x04 + 6] << 8  |
                       mdhd[mdhd_timescale_offset + 0x04 + 7];
        }
        else {
            mdhd_timescale_offset  = 0x10;
            mpu->media_duration = mdhd[mdhd_timescale_offset + 0x04 + 0] << 24 |
                       mdhd[mdhd_timescale_offset + 0x04 + 1] << 16 |
                       mdhd[mdhd_timescale_offset + 0x04 + 2] << 8  |
                       mdhd[mdhd_timescale_offset + 0x04 + 3];
        }
        mpu->timescale = mdhd[mdhd_timescale_offset + 0] << 24 | 
                    mdhd[mdhd_timescale_offset + 1] << 16 | 
                    mdhd[mdhd_timescale_offset + 2] <<  8 | 
                    mdhd[mdhd_timescale_offset + 3];

        
    }
    return 1;
}

static unsigned char* smt_mp4_time_info(unsigned char* mp4buffer, unsigned int length, int type) {
    int i;
    int bytecount;
    if(type == 0)
    {
        int mdhd_offset = smt_find_field(mp4buffer ,length,"mdhd", 4);
        if(mdhd_offset  >= 0) {
            unsigned char* mdhd = mp4buffer  + mdhd_offset; 
            int mdhd_timescale_offset = 0;
            int mdhd_duration_offset = 0;
            int version = (mdhd+0x04)[0];
            int64_t duration = 0;
            int32_t timescale = 0;
            if(version) {
                mdhd_timescale_offset = 0x18;
                bytecount = 8;
                for(i = 0; i++; i < bytecount) 
                    duration |= mdhd[mdhd_timescale_offset + 0x04 + i] << ( 8 * (bytecount -1 - i));
                //duration  = *((int64_t*)(mdhd + mdhd_timescale_offset + 0x04));
            }
            else {
                mdhd_timescale_offset  = 0x10;
                bytecount = 4;
                duration = mdhd[mdhd_timescale_offset + 0x04 + 0] << 24 |
                           mdhd[mdhd_timescale_offset + 0x04 + 1] << 16 |
                           mdhd[mdhd_timescale_offset + 0x04 + 2] << 8  |
                           mdhd[mdhd_timescale_offset + 0x04 + 3];
#if 0
                for(i = 0; i++; i < bytecount) 
                    duration |= mdhd[mdhd_timescale_offset + 0x04 + i] << ( 8 * (bytecount -1 - i));
                    //duration  = *((int32_t*)(mdhd + mdhd_timescale_offset +  0x04));
#endif
            }
            mdhd_duration_offset  += 0x04;
            timescale = mdhd[mdhd_timescale_offset]<<24 | mdhd[mdhd_timescale_offset+1]<<16 | mdhd[mdhd_timescale_offset+2]<<8 | mdhd[mdhd_timescale_offset+3];

#if 0
            bytecount = 4;
            for(i = 0; i++; i < bytecount) 
                timescale  |= ((int32_t)(mdhd[mdhd_timescale_offset  + i]) << ( 8 * (bytecount -1 - i)));
#endif
            //av_log(NULL, AV_LOG_ERROR, "0 mpu_duration = %lld ; timescale =%ld\n", duration , *((int32_t*)(mdhd + mdhd_timescale_offset)));
            //av_log(NULL, AV_LOG_ERROR, "\nmpu_duration = %lld ; timescale =%ld\n", duration ,timescale);
            //return mdhd;
        }

    }
    else
    {
        int tfdt_offset = smt_find_field(mp4buffer ,length,"tfdt", 4);
        if(tfdt_offset  >= 0) {
            unsigned char* tfdt = mp4buffer  + tfdt_offset; 
            int64_t baseMediaDecoderTime = 0;
            int version = (tfdt+0x04)[0];
            if(version) {
                bytecount = 8;
            }
            else {
                bytecount = 4;
            }
            baseMediaDecoderTime = tfdt[ 0x08 + 0] << 56 |
                                   tfdt[ 0x08 + 1] << 48 |
                                   tfdt[ 0x08 + 2] << 40 |
                                   tfdt[ 0x08 + 3] << 32 |
                                   tfdt[ 0x08 + 4] << 24 |
                                   tfdt[ 0x08 + 5] << 16 |
                                   tfdt[ 0x08 + 6] << 8  |
                                   tfdt[ 0x08 + 7] ;
            //for(i = 0; i++; i < bytecount) 
            //    baseMediaDecoderTime |= tfdt[ 0x08 + i] << ( 8 * (bytecount -1 - i));
            //av_log(NULL, AV_LOG_ERROR, "\nmpu baseMediaDecoderTime=  %lld\n", baseMediaDecoderTime);

        }
    }
    return NULL;

}

static smt_status smt_assemble_mpu(URLContext *h, smt_receive_entity *recv, int asset_id, smt_mpu *mpu)
{
    smt_packet *iterator;
	unsigned int mpu_head_pkt_first_seq,mpu_head_pkt_last_seq, moov_head_pkt_first_seq, moov_head_pkt_last_seq;
    unsigned int mpu_h_offset = 0, moof_h_offset = 0, data_offset = 0;
	int sample_index = 0;
	enum phase_status{
		phase_error = -1,
		phase_mpu_meta_begin,
		phase_mpu_meta_continue,
		phase_moof_meta_begin,
		phase_moof_meta_continue,
		phase_mfu
	} status = phase_mpu_meta_begin;

	iterator = recv->mpu_head[asset_id];
	unsigned int expected_pkt_seq = iterator->packet_sequence_number;
	
    while(iterator){
        smt_payload_mpu *pld = (smt_payload_mpu *)&(iterator->payload);
        switch(status){
			case phase_mpu_meta_begin:{
				if(expected_pkt_seq != iterator->packet_sequence_number){
					av_log(h, AV_LOG_ERROR, "mpu metadata packets %d ~ %d are lost. \n", expected_pkt_seq, iterator->packet_sequence_number - 1);
					return SMT_STATUS_ERROR;
				}
				if(pld->FT == mpu_metadata){
					if(pld->f_i == complete_data){
                        mpu_head_pkt_first_seq = mpu_head_pkt_last_seq = iterator->packet_sequence_number;
						status = phase_moof_meta_begin;
					}else if(pld->f_i == first_fragment){
					    mpu_head_pkt_first_seq = iterator->packet_sequence_number;
						status = phase_mpu_meta_continue;
					}else{
						av_log(h, AV_LOG_ERROR, "first payload fragmentation indicator number is %d, mpu metadata is incomplete!\n", pld->f_i);
						return SMT_STATUS_ERROR;
					}	
				}else{
					av_log(h, AV_LOG_ERROR, "first payload MPU fragement type is %d, no mpu MPU metadata!\n", pld->FT);
					return SMT_STATUS_ERROR;			
				}
				mpu->mpu_header_length += pld->data_len;
				expected_pkt_seq++;
	        	iterator = iterator->next;
				break;
			}case phase_mpu_meta_continue:{
				if(expected_pkt_seq != iterator->packet_sequence_number){
					av_log(h, AV_LOG_ERROR, "mpu metadata packets %d ~ %d are lost. \n", expected_pkt_seq, iterator->packet_sequence_number - 1);
					return SMT_STATUS_ERROR;
				}
				if(pld->FT == mpu_metadata){
					if(pld->f_i == middle_fragment)
						status = phase_mpu_meta_continue;
					else if(pld->f_i == last_fragment){
                        mpu_head_pkt_last_seq = iterator->packet_sequence_number;
						status = phase_moof_meta_begin;
					}else{
						av_log(h, AV_LOG_ERROR, "phase_mpu_meta_continue expect %d or %d but %d \n", middle_fragment, last_fragment, pld->f_i);
						return SMT_STATUS_ERROR;
					}
				}else{
					av_log(h, AV_LOG_ERROR, "%d in phase_mpu_meta_continue\n", pld->FT);
					return SMT_STATUS_ERROR;
				}
				mpu->mpu_header_length += pld->data_len;
				expected_pkt_seq++;
	        	iterator = iterator->next;
				break;
			}case phase_moof_meta_begin:{
				if(expected_pkt_seq != iterator->packet_sequence_number){
					av_log(h, AV_LOG_ERROR, "MOOF metadata packets %d ~ %d are lost. \n", expected_pkt_seq, iterator->packet_sequence_number - 1);
					return SMT_STATUS_ERROR;
				}
				if(pld->FT == movie_fragment_metadata){
					if(pld->f_i == complete_data){
                        moov_head_pkt_first_seq = moov_head_pkt_last_seq = iterator->packet_sequence_number;
						status = phase_mfu;
					}else if(pld->f_i == first_fragment){
					    moov_head_pkt_first_seq = iterator->packet_sequence_number;
						status = phase_moof_meta_continue;
					}else{
						av_log(h, AV_LOG_ERROR, "first payload fragmentation indicator number is %d, moof metadata is incomplete!\n", pld->f_i);
						return SMT_STATUS_ERROR;
					}
				}else{
					av_log(h, AV_LOG_ERROR, "first payload MOOF fragement type is %d, no mpu MOOV metadata!\n", pld->FT);
					return SMT_STATUS_ERROR;
				}
				mpu->moof_header_length += pld->data_len;
				expected_pkt_seq++;
	        	iterator = iterator->next;
				break;
			}case phase_moof_meta_continue:{
				if(expected_pkt_seq != iterator->packet_sequence_number){
					av_log(h, AV_LOG_ERROR, "MOOF metadata packets %d ~ %d are lost. \n", expected_pkt_seq, iterator->packet_sequence_number - 1);
					return SMT_STATUS_ERROR;
				}
				if(pld->FT == movie_fragment_metadata){
					if(pld->f_i == middle_fragment)
						status = phase_moof_meta_continue;
					else if(pld->f_i == last_fragment){
                        moov_head_pkt_last_seq = iterator->packet_sequence_number;
						status = phase_mfu;
					}else{
						av_log(h, AV_LOG_ERROR, "phase_moof_meta_continue expect %d or %d but %d \n", middle_fragment, last_fragment, pld->f_i);
						return SMT_STATUS_ERROR;
					}
				}else{
					av_log(h, AV_LOG_ERROR, "%d in phase_moof_meta_continue\n", pld->FT);
					return SMT_STATUS_ERROR;
				}
				mpu->moof_header_length += pld->data_len;
				expected_pkt_seq++;
	        	iterator = iterator->next;
				break;
			}case phase_mfu:{
			    int sample_size, default_sample_size;
                if(mfu != pld->FT){
                    av_log(h, AV_LOG_ERROR, "wrong payload type = %d\n", pld->FT);
                    return SMT_STATUS_ERROR;
                }
				if(!mpu->mpu_header_data || !mpu->moof_header_data){
					// assmeble mpu header and moof header
					int seq = recv->mpu_head[asset_id]->packet_sequence_number;
					smt_packet *it = recv->mpu_head[asset_id];
					mpu->mpu_header_data = (unsigned char *)av_mallocz(mpu->mpu_header_length);
					mpu->moof_header_data = (unsigned char *)av_mallocz(mpu->moof_header_length);
					
					while(seq <= moov_head_pkt_last_seq){
						smt_payload_mpu *pld = (smt_payload_mpu *)(&it->payload);
						if(seq >= mpu_head_pkt_first_seq && seq <= mpu_head_pkt_last_seq){
							memcpy(mpu->mpu_header_data + mpu_h_offset, pld->data, pld->data_len);
							mpu_h_offset += pld->data_len;
						}else if(seq >= moov_head_pkt_first_seq && seq <= moov_head_pkt_last_seq){
							memcpy(mpu->moof_header_data + moof_h_offset, pld->data, pld->data_len);
							moof_h_offset += pld->data_len;
						}
						it = it->next;
						seq = it->packet_sequence_number;
					}

                    //check track id in mpu header     
					int tkhd_offset = smt_find_field(mpu->mpu_header_data ,mpu->mpu_header_length,"tkhd", 4);
					if(tkhd_offset >= 0){
						//av_log(h, AV_LOG_WARNING, "tkhd_offset = %x\n", tkhd_offset);
						unsigned char* tkhd = mpu->mpu_header_data + tkhd_offset; // the offset from beginning to tkhd is 0xb9 in mpu format file.
						int track_id_offset = 0;
						int version = (tkhd+0x04)[0];
						if(version)
							track_id_offset = 0x18;
						else
							track_id_offset = 0x10;
						unsigned char *track_id_ptr = tkhd + track_id_offset;
						int track_id = (track_id_ptr[0] << 24 | track_id_ptr[1] << 16 | track_id_ptr[2] << 8 | track_id_ptr[3]);
						track_id_ptr[0] = (asset_id + MEDIA_TRACK_ID_OFFSET) >> 24;
						track_id_ptr[1] = (asset_id + MEDIA_TRACK_ID_OFFSET) >> 16;
						track_id_ptr[2] = (asset_id + MEDIA_TRACK_ID_OFFSET) >> 8;
						track_id_ptr[3] = (asset_id + MEDIA_TRACK_ID_OFFSET);
				 	}

					int trex_offset = smt_find_field(mpu->mpu_header_data+tkhd_offset,mpu->mpu_header_length-tkhd_offset,"trex", 4);
					if(trex_offset >= 0){
						trex_offset += tkhd_offset;
						//av_log(h, AV_LOG_WARNING, "trex_offset = %x\n", trex_offset);
						unsigned char* trex = mpu->mpu_header_data + trex_offset; // the offset from beginning to tkhd is 0xb9 in mpu format file.
						unsigned char *track_id_ptr = trex + 0x08;
						int track_id = (track_id_ptr[0] << 24 | track_id_ptr[1] << 16 | track_id_ptr[2] << 8 | track_id_ptr[3]);
						track_id_ptr[0] = (asset_id + MEDIA_TRACK_ID_OFFSET) >> 24;
						track_id_ptr[1] = (asset_id + MEDIA_TRACK_ID_OFFSET) >> 16;
						track_id_ptr[2] = (asset_id + MEDIA_TRACK_ID_OFFSET) >> 8;
						track_id_ptr[3] = (asset_id + MEDIA_TRACK_ID_OFFSET);
				 	}


					
                    //check track id in moof again
                    int tfhd_offset = smt_find_field(mpu->moof_header_data ,mpu->moof_header_length,"tfhd", 4);
					if(tkhd_offset >= 0){
						//av_log(h, AV_LOG_WARNING, "tfhd_offset = %x\n", trex_offset);
						unsigned char* tfhd = mpu->moof_header_data + tfhd_offset; //  the offset from moof to tfhd is 0x24 in mpu format file.
						int flag = (tfhd[5] << 16) | (tfhd[6] << 8) | tfhd[7];
						int track_id_offset = 0x08;
                        int default_sample_size_offset = track_id_offset + 4;
                        if(flag & 0x01)
                            default_sample_size_offset += 8; //base time offset field
                        if(flag & 0x08)
                            default_sample_size_offset += 4; //default sample duration field 
                        
						unsigned char *track_id_ptr = tfhd + track_id_offset;
                        unsigned char *dafault_sample_size_ptr = tfhd + default_sample_size_offset;
                        
						//int track_id = (track_id_ptr[0] << 24 | track_id_ptr[1] << 16 | track_id_ptr[2] << 8 | track_id_ptr[3]);
						track_id_ptr[0] = (asset_id + MEDIA_TRACK_ID_OFFSET) >> 24;
						track_id_ptr[1] = (asset_id + MEDIA_TRACK_ID_OFFSET) >> 16;
						track_id_ptr[2] = (asset_id + MEDIA_TRACK_ID_OFFSET) >> 8;
						track_id_ptr[3] = (asset_id + MEDIA_TRACK_ID_OFFSET);

                        default_sample_size = (dafault_sample_size_ptr[0] << 24 | dafault_sample_size_ptr[1] << 16 | dafault_sample_size_ptr[2] << 8 | dafault_sample_size_ptr[3]);
					}
					//prepare sample buffer
					unsigned char *mdat_size = mpu->moof_header_data + (mpu->moof_header_length - 8);
					mpu->sample_length = (mdat_size[0] << 24 | mdat_size[1] << 16 | mdat_size[2] << 8 | mdat_size[3]) - 8; //exclude mdat header
					mpu->sample_data = (unsigned char *)av_mallocz(mpu->sample_length);
				}

				int trun_offset = smt_find_field(mpu->moof_header_data ,mpu->moof_header_length,"trun", 4);
				unsigned char *trun = mpu->moof_header_data + trun_offset; // trun offset - moof offset = 0x40
				int flag = (trun[5] << 16) | (trun[6] << 8) | trun[7];
				unsigned char *buf = trun + 0x0c;
				if(flag & 0x01)
					buf += 4;
				if(flag & 0x04)
					buf += 4;

                if(flag & 0x200){
    				int ext_duration = (flag & 0x100) ? 4 : 0;
                    int ext_flags = (flag & 0x400) ? 4: 0;
                    int ext_cts = (flag & 0x800) ? 4: 0;
    				int offset = (4 + ext_duration + ext_flags + ext_cts) * sample_index + ext_duration ;  //skip sample duration, flags and cts if fields exist
    				sample_size = buf[offset] << 24 | buf[offset+1] << 16 | buf[offset+2] << 8 | buf[offset+3];
                }else{
                    sample_size = default_sample_size;
                }
                if(sample_size < MIN_PACKET_SIZE)
                    av_log(h, AV_LOG_WARNING, "asset %d packet length is %d, time = %llu\n",asset_id, sample_size, av_gettime());
                if(sample_size > mpu->sample_length){
                    av_log(h, AV_LOG_ERROR, "single sample size %d more than madt size %d\n", sample_size, mpu->sample_length);
                    return SMT_STATUS_ERROR;
                }
                int sample_process_finish = 1;
			
				if(sample_index < pld->DU_Header[0].sample_number - 1){ //sample number is started from 1
					av_log(h, AV_LOG_WARNING, "packets %d ~ %d are lost. all packets in sample %d lost\n",
						iterator->previous->packet_sequence_number + 1, 
						iterator->packet_sequence_number - 1,
						sample_index);
				}else if(sample_index == pld->DU_Header[0].sample_number - 1){
					if(pld->f_i == complete_data){
						av_assert0(sample_size == pld->data_len);
						memcpy(mpu->sample_data + data_offset, pld->data, sample_size); //copy from packet directly
						iterator = iterator->next;
					}else if(pld->f_i == first_fragment){
					    int good_sample = 0;
						unsigned int pos = 0;
						unsigned char *sample_buf = (unsigned char *)av_mallocz(sample_size);
                        int temp_sample_size = sample_size; 
                        if(!sample_buf){
                            av_log(h, AV_LOG_ERROR, "malloc sample buffer failed\n");
                            return SMT_STATUS_ERROR;
                        }
						do{
                            if(pos + pld->data_len > temp_sample_size){
                                temp_sample_size = pos + pld->data_len;
                                sample_buf = (unsigned char *)av_realloc(sample_buf, temp_sample_size);
                            }
							memcpy(sample_buf + pos, pld->data, pld->data_len);
							pos += pld->data_len;
							int seq = iterator->packet_sequence_number;
							iterator = iterator->next;
							if(pld->f_i == last_fragment)
								break;
							if(!iterator)
								break;
							if(iterator->packet_sequence_number - seq == 1){
								good_sample = 1;
							}else if(iterator->packet_sequence_number - seq > 1){
								av_log(h, AV_LOG_WARNING, "packets %d ~ %d are lost.\n",
									iterator->previous->packet_sequence_number + 1, 
									iterator->packet_sequence_number - 1);
								good_sample = 0;
                                if(pld->DU_Header[0].sample_number - 1 == sample_index)
                                    sample_process_finish = 0;
							}else{
								av_log(h, AV_LOG_ERROR, "packet sequence(%d) can not smaller than previous(%d).\n", iterator->packet_sequence_number, seq);
								av_assert0(0);
							}
							pld = (smt_payload_mpu *)&(iterator->payload);
						}while(good_sample);

						if(good_sample){  //only copy good sample
						    int copy_len = sample_size;
							//av_assert0(pos == sample_size || !iterator);
							if(pos != sample_size){
                                //char dbgfn[256] = {0};
                                //sprintf(dbgfn, "../../../../Temp/dbginfo_%x_%x_%x_%d.data", mpu->sample_length, sample_size, pos, sample_index);
                                //avformat_dump(dbgfn, mpu->mpu_header_data, mpu->mpu_header_length, "a+");
                                //avformat_dump(dbgfn, mpu->moof_header_data, mpu->moof_header_length, "a+");
                                av_log(h, AV_LOG_ERROR, "last fragment position(%u) which in not equal sample size(%u),time = %llu\n",pos, sample_size, av_gettime());
                                //av_freep(&sample_buf);
                                //return SMT_STATUS_ERROR;
                                if(pos < sample_size)
                                    copy_len = pos;
                            }
                            
                            memcpy(mpu->sample_data + data_offset, sample_buf, copy_len);
                            //av_log(h, AV_LOG_INFO, "sample %d is a good sample, sample_size = %d\n",sample_index, sample_size);
						}
						av_freep(&sample_buf);
					}else{
						int counter = 0;
						while(pld->f_i != first_fragment && pld->f_i != complete_data){ //skip useless packets
							iterator = iterator->next;
							if(!iterator)
								return SMT_STATUS_OK;
							pld = (smt_payload_mpu *)&(iterator->payload);
							counter++;
						}
						av_log(h, AV_LOG_WARNING, "packets for sample %d of %s MPU %d is lost. %d packets are skipped \n", 
                            sample_index, asset_id?"video":"audio", pld->MPU_sequence_number, counter - 1);
					}

				}else{
					av_assert0(0);
				}

                if(sample_process_finish){
    				data_offset += sample_size;
    				sample_index++;
                }
				break;
			}
		}
		
    }

    return SMT_STATUS_OK;

}

static void smt_release_buffer(URLContext *h, smt_receive_entity *recv, int asset)
{
	smt_packet *iterator = recv->mpu_head[asset];
	while(iterator){
		smt_packet *tmp = iterator->next;
		switch(iterator->type){
			case mpu_payload:{
				smt_payload_mpu *pld = (smt_payload_mpu *)(&iterator->payload);
				if(pld->data)
					av_freep(&pld->data);
				break;
			}case gfd_payload:{
				smt_payload_gfd *pld = (smt_payload_gfd *)(&iterator->payload);
				if(pld->data)
					av_freep(&pld->data);
				break;
			}case sig_payload:{
				smt_payload_sig *pld = (smt_payload_sig *)(&iterator->payload);
				if(pld->data)
					av_freep(&pld->data);
				break;
			}case repair_symbol_payload:{
				smt_payload_id *pld = (smt_payload_id *)(&iterator->payload);
				if(pld->data)
					av_freep(&pld->data); 
				break;
			}
		}
		av_freep(&iterator);
		iterator = tmp;
	}
}

static smt_status smt_add_mpu_packet(URLContext *h, smt_receive_entity *recv, smt_packet *p)
{
    int asset_id = p->packet_id;
    smt_status ret = SMT_STATUS_OK;
    if(!recv->mpu_head[asset_id]){
        recv->mpu_head[asset_id] = p;
    }else{
        smt_payload_mpu *pld_f, *pld;
        pld_f = (smt_payload_mpu *)&(recv->mpu_head[asset_id]->payload);
        pld = (smt_payload_mpu *)&(p->payload);
        if(pld_f->MPU_sequence_number < pld->MPU_sequence_number){
                smt_packet *iterator;
                smt_mpu *mpu = (smt_mpu *)av_mallocz(sizeof(smt_mpu));
                mpu->asset = asset_id;
                mpu->sequence = pld_f->MPU_sequence_number;
                iterator = recv->mpu_head[asset_id];//get first smt packet of the mpu
                unsigned int timestamp_of_first_packet = iterator->timestamp;

                ret = smt_assemble_mpu(h, recv, asset_id, mpu);
                if(ret != SMT_STATUS_OK){
                    av_log(h, AV_LOG_ERROR, "assemble mpu %d failed!\n\n", pld_f->MPU_sequence_number);
                    smt_release_mpu(h, mpu);
                }else{
                    if(mpu->mpu_header_data == NULL || mpu->moof_header_data == NULL || mpu->sample_data == NULL) {
                        av_log(h, AV_LOG_ERROR, "\n mpu have invalid data, mpu_header_data=0x%x, moof_header_data=0x%x, sample_data=0x%x\n",
                                                mpu->mpu_header_data,
                                                mpu->moof_header_data,
                                                mpu->sample_data);
                        smt_release_mpu(h, mpu);
                        smt_release_buffer(h, recv, asset_id);
                        recv->mpu_head[asset_id] = p;
                        return SMT_STATUS_ERROR;
                    }
#ifdef SMT_DUMP
                    char fn[256];
                    memset(fn, 0, 256);
                    sprintf(fn, "../../../../Temp/mpu_%d_%s.mpu", pld_f->MPU_sequence_number, asset_id?"v":"a");
                    avformat_dump(fn, mpu->mpu_header_data, mpu->mpu_header_length, "a+");
                    avformat_dump(fn, mpu->moof_header_data, mpu->moof_header_length, "a+");
                    avformat_dump(fn, mpu->sample_data, mpu->sample_length, "a+");
                    av_log(h, AV_LOG_INFO, "%s is generated\n",fn, pld->MPU_sequence_number, asset_id);
#endif


                    { 
                        int64_t cur_begin_time_value;
                        int64_t now_time_us =  av_gettime();
                        time_t  now_time_s = (time_t)(now_time_us/ (1000*1000));//time(NULL);
                        char* device = NULL;
                        struct tm today_zero_time = *localtime(&now_time_s);
                        today_zero_time.tm_hour = 0;
                        today_zero_time.tm_min  = 0;
                        today_zero_time.tm_sec  = 0;
                        time_t time_zero_s = mktime(&today_zero_time);
                        int64_t time_zero_us = now_time_us - ((int64_t)(now_time_s - time_zero_s ))*1000*1000 - (now_time_us%(1000*1000));

                        cur_begin_time_value = time_zero_us / 1000 + (int64_t)timestamp_of_first_packet; 
                        if( 0 == smt_callback_entity.get_begin_time(h, p->packet_id) )  { 
                            smt_callback_entity.set_begin_time(h, p->packet_id, cur_begin_time_value);
                                
                        av_log(h, AV_LOG_INFO, "\n set asset_id=%d(mpu=%d) begin_time=%lld\n",
                                        p->packet_id, 
                                        pld_f->MPU_sequence_number,
                                        cur_begin_time_value );
                        }


                    }

                    smt_parse_media_info(mpu);


                    if(smt_callback_entity.mpu_callback_fun)
                        smt_callback_entity.mpu_callback_fun(h, mpu);
                    else
                        smt_release_mpu(h, mpu);
                }
                
                smt_release_buffer(h, recv, asset_id);
                recv->mpu_head[asset_id] = p;
                return ret;
        }
        
        if(p->packet_sequence_number < recv->mpu_head[asset_id]->packet_sequence_number){
            p->next = recv->mpu_head[asset_id];
            recv->mpu_head[asset_id]->previous = p;
            recv->mpu_head[asset_id] = p;
        }else{
            smt_packet *iterator = recv->mpu_head[asset_id];
            do{
                if(p->packet_sequence_number == iterator->packet_sequence_number){
                    av_log(h, AV_LOG_ERROR, "duplicate packet sequence number. seq_num = %d\n", p->packet_sequence_number);
                    return SMT_STATUS_ERROR;
                }

                if(iterator->next == NULL){
                    iterator->next = p;
                    p->previous = iterator;
                    return SMT_STATUS_OK;
                }else if(iterator->packet_sequence_number < p->packet_sequence_number && iterator->next->packet_sequence_number > p->packet_sequence_number){
                    p->next = iterator->next;
                    iterator->next->previous = p;
                    p->previous = iterator;
                    iterator->next = p;
                    return SMT_STATUS_OK;
                }
                iterator = iterator->next;
            }while(iterator);
        }
    }
    return ret;

}


#ifdef SMT_PROTOCAL_SIGNAL
long signalling_message_segment_append(signalling_message_buf_t *p_signalling_message, void *data,  u_int32_t length) {
    if(!p_signalling_message) return INVALID_SIGNALLING_MESSAGE;
    if(!data) return INVALID_DATA;
    if(p_signalling_message->signal_seekpoint+ length > p_signalling_message->length) {
        printf("fatal error!!!! memcpy signalling message error -3\n"); 
        return OUT_OF_RANGE;
    }
    //printf("seekpoint= %d\n",p_signalling_message->signal_seekpoint);
    memcpy(p_signalling_message->signal_buf + p_signalling_message->signal_seekpoint, data, length);
    p_signalling_message->signal_seekpoint += length;
    return length;
}

signalling_message_buf_t *p_signalling_message_buf = NULL;
static smt_status smt_add_sig_packet(URLContext *h,smt_receive_entity *recv, smt_packet *p)
{
    smt_status ret = SMT_STATUS_OK;     
    int signal_size = 0;
    long receive_time;
	
    int asset_id = p->packet_id;
    smt_payload_sig *sig_payload = (smt_payload_sig *)&(p->payload);//

    unsigned char *payload_data = sig_payload->data; //
    if(sig_payload->f_i == complete_data)
    {
    }
    if(sig_payload->f_i == first_fragment)
    {
        if(p_signalling_message_buf) {
            if(p_signalling_message_buf->signal_buf){
                free(p_signalling_message_buf->signal_buf);
                p_signalling_message_buf->signal_buf = NULL;
            }
            free(p_signalling_message_buf);
            p_signalling_message_buf = NULL;
        }
        pa_message_t pa_message;
        read_pa_message_header(&pa_message,(const char*)payload_data);
        p_signalling_message_buf = (signalling_message_buf_t*)av_mallocz(sizeof(signalling_message_buf_t));
        if(p_signalling_message_buf == NULL){
            puts("Memory allocation failed.");
            exit(EXIT_FAILURE);
        }
        memset(p_signalling_message_buf, 0, sizeof(signalling_message_buf_t));
        //add by drj
        p_signalling_message_buf->length = pa_message.length+PAh_BUFF_LEN;
        //add by drj
        p_signalling_message_buf->signal_buf = (unsigned char*)av_mallocz((pa_message.length+PAh_BUFF_LEN)*sizeof(unsigned char));
        signalling_message_segment_append(p_signalling_message_buf, payload_data, sig_payload->MSG_length);
    }
    if(sig_payload->f_i == middle_fragment)
    {
        signalling_message_segment_append(p_signalling_message_buf, payload_data, sig_payload->MSG_length);
    }
    if(sig_payload->f_i == last_fragment)
    {
        signalling_message_segment_append(p_signalling_message_buf, payload_data, sig_payload->MSG_length);
        smt_sig * sig = av_mallocz(sizeof(smt_sig));
        read_pa_message(sig,(const char*)p_signalling_message_buf->signal_buf);
        if(p_signalling_message_buf) {
            if(p_signalling_message_buf->signal_buf) {
                free(p_signalling_message_buf->signal_buf);
                p_signalling_message_buf->signal_buf = NULL;
            }
            free(p_signalling_message_buf); 
            p_signalling_message_buf = NULL;
        }
        //printf("id = %u version = %u, length = %u numberoftables = %u\n",sig->message_id,sig->version,sig->length,sig->number_of_tables);
        if(smt_callback_entity.sig_callback_fun)
            smt_callback_entity.sig_callback_fun(h,sig);
    } 
    // if (pa_message.mp_table.MP_table_asset[0].asset_descriptors_length != 0)
    // {
    //     edit_list_t edit_list_id;
    //     int id_new=0;
    //     int mpu_new=0;
    //     id_change(edit_list_id,id_new,mpu_new);
    // }
    return ret; //
}
// -----------------function edit_list-----------------
int info_change(int id_new, int mpu_new)
{
    printf("id_old=%d id_new=%d\n",g_id_old,id_new);
    printf("mpu_old=%d,mpu_new=%d\n",g_mpu_old,mpu_new);
    return  0; //
}

int id_change(edit_list_t edit_list_id,int id_new,int mpu_new)
 {
//     MUR_descriptors_t mur_descriptor;
//     char* mur_buf=(char *)malloc(pa_message.mp_table.MP_table_asset[0].asset_descriptors_length*sizeof(char));
//     memcpy(mur_buf,pa_message.mp_table.MP_table_asset[0].asset_descriptors_byte,pa_message.mp_table.MP_table_asset[0].asset_descriptors_length);
//     read_MUR_descriptors(&mur_descriptor,mur_buf);
//     int i;
//     for(i=0;i<EDIT_LIST_NUM;i++)
//     {
//         id_new=mur_descriptor.edit_list[i].edit_list_id;
//         mpu_new=mur_descriptor.edit_list[i].mpu_sequence_number;
//     }
//         if (g_id_old==-1)
//     {
//         g_id_old=id_new;
//     }
//     if (id_new==g_id_old)
//     {
//         ;
//     }
//     else
//     {
//         info_change(id_new,mpu_new);
//     }
//     g_id_old=id_new;
//     g_mpu_old=mpu_new;
//     free(mur_buf);
//     mur_buf=NULL;
    return 0;
}
// -----------------function edit_list-----------------
#endif

static smt_status smt_add_packet(URLContext *h, smt_receive_entity *recv, smt_packet *p)
{
    smt_status ret = SMT_STATUS_OK;
    int pkt_id = p->packet_id;
    smt_payload_type tp = p->type;
    if(pkt_id >= MAX_ASSET_NUMBER 
#ifdef SMT_PROTOCAL_SIGNAL
            && pkt_id != Signal_PACKET_ID
#endif
            ){
        printf("error!\n");
        av_log(h, AV_LOG_ERROR, "current asset number is %d, which is bigger than MAX_ASSET_NUMBER!\n", pkt_id);
        recv->packet_parser_status = SMT_STATUS_INIT;
        return SMT_STATUS_NOT_SUPPORT;
    }
    
    switch(tp){
        case mpu_payload:
            ret = smt_add_mpu_packet(h, recv, p);
            break;
        case gfd_payload:
            break;
        case sig_payload:
#ifdef SMT_PROTOCAL_SIGNAL
            ret = smt_add_sig_packet(h, recv, p);
#endif
            break;
        case repair_symbol_payload:
            break;
    }
    return ret;
}

static smt_status smt_assemble_packet_header(URLContext *h, smt_send_entity *snd, unsigned char *buffer, smt_packet *pkt)
{

    buffer[0] = (pkt->R&0x01) | (pkt->X&0x01) << 1 | (pkt->r&0x01) << 2 | (pkt->FEC&0x03) << 3 | (pkt->C&0x01) << 5 | (pkt->V&0x03) << 6;
    buffer[1] = (pkt->type&0x3f) | (pkt->RES&0x03)<<6;
	buffer[2] = (unsigned char)(pkt->packet_id >> 8);
	buffer[3] = (unsigned char)(pkt->packet_id);
	buffer[4] = (unsigned char)(pkt->timestamp >> 24);
	buffer[5] = (unsigned char)(pkt->timestamp >> 16); 
	buffer[6] = (unsigned char)(pkt->timestamp >> 8); 
	buffer[7] = (unsigned char)(pkt->timestamp);
	buffer[8] = (unsigned char)(pkt->packet_sequence_number >> 24);
	buffer[9] = (unsigned char)(pkt->packet_sequence_number >> 16); 
	buffer[10] = (unsigned char)(pkt->packet_sequence_number >> 8); 
	buffer[11] = (unsigned char)(pkt->packet_sequence_number);
	buffer[12] = (unsigned char)(pkt->packet_counter >> 24);
	buffer[13] = (unsigned char)(pkt->packet_counter >> 16); 
	buffer[14] = (unsigned char)(pkt->packet_counter >> 8); 
	buffer[15] = (unsigned char)(pkt->packet_counter);
	if(pkt->X && pkt->header_extension.header_extension_value) {
		buffer[16] = (unsigned char)(pkt->header_extension.type>> 8);
		buffer[17] = (unsigned char)(pkt->header_extension.type);
		buffer[18] = (unsigned char)(pkt->header_extension.length>> 8);
		buffer[19] = (unsigned char)(pkt->header_extension.length);
		memset(buffer + 19, 0, pkt->header_extension.length);
		memcpy(buffer + 19, pkt->header_extension.header_extension_value, pkt->header_extension.length);
	}
    snd->pkt_counter++;
	return 0;
}

static smt_status smt_assemble_payload_header(URLContext *h, unsigned char *buffer, smt_payload_mpu *pld)
{
	buffer[0] = (unsigned char)(pld->length >> 8);
	buffer[1] = (unsigned char)(pld->length);
	buffer[2] = (pld->A&0x01) | (pld->f_i&0x03) << 1 | (pld->T&0x01) << 3 | (pld->FT&0x0f) << 4;
	buffer[3] = (unsigned char)pld->frag_counter;
	buffer[4] = (unsigned char)((0xff000000 & pld->MPU_sequence_number) >> 24);
	buffer[5] = (unsigned char)((0xff0000 & pld->MPU_sequence_number) >> 16);
	buffer[6] = (unsigned char)((0xff00 & pld->MPU_sequence_number) >> 8);
	buffer[7] = (unsigned char)(0xff & pld->MPU_sequence_number);
    if(pld->A == 0){
    	if(pld->FT == mfu) {
    		buffer[8]  = (unsigned char)((0xff000000 & pld->DU_Header[0].movie_fragment_sequence_number) >> 24);
    		buffer[9]  = (unsigned char)((  0xff0000 & pld->DU_Header[0].movie_fragment_sequence_number) >> 16);
    		buffer[10] = (unsigned char)((    0xff00 & pld->DU_Header[0].movie_fragment_sequence_number) >> 8);
    		buffer[11] = (unsigned char)(       0xff & pld->DU_Header[0].movie_fragment_sequence_number);
    		buffer[12] = (unsigned char)((0xff000000 & pld->DU_Header[0].sample_number) >> 24);
    		buffer[13] = (unsigned char)((  0xff0000 & pld->DU_Header[0].sample_number) >> 16);
    		buffer[14] = (unsigned char)((    0xff00 & pld->DU_Header[0].sample_number) >> 8);
    		buffer[15] = (unsigned char)(       0xff & pld->DU_Header[0].sample_number);
    		buffer[16] = (unsigned char)((0xff000000 & pld->DU_Header[0].offset) >> 24);
    		buffer[17] = (unsigned char)((  0xff0000 & pld->DU_Header[0].offset) >> 16);
    		buffer[18] = (unsigned char)((    0xff00 & pld->DU_Header[0].offset) >> 8);
    		buffer[19] = (unsigned char)(       0xff & pld->DU_Header[0].offset);
    		buffer[20] = (unsigned char)pld->DU_Header[0].priority;
    		buffer[21] = (unsigned char)pld->DU_Header[0].dep_counter;
    	}
    }else
        av_log(h, AV_LOG_ERROR, "payload aggregation is not supported now!");
	return 0;
}


smt_status smt_parse(URLContext *h, smt_receive_entity *recv, unsigned char* buffer, int *p_size)
{
    smt_status status;
    unsigned char *payload_data = NULL;
    smt_packet *packet;
    int size = *p_size;

    if(!buffer || !size)
        return SMT_STATUS_INVALID_INPUT;
#ifdef SMT_NET_STATE		
    /* for delay infomation */
    if(buffer[3] == 8) {
        int64_t send_time = 0;
        char* device = NULL;
        send_time = (int64_t)buffer[16] << 56 | 
                    (int64_t)buffer[17] << 48 | 
                    (int64_t)buffer[18] << 40 |
                    (int64_t)buffer[19] << 32 |
                    (int64_t)buffer[20] << 24 | 
                    (int64_t)buffer[21] << 16 | 
                    (int64_t)buffer[22] << 8  |
                    (int64_t)buffer[23];
        int64_t delay = av_gettime() - send_time;
        char fname[128] = {0};
        memcpy(fname, &(buffer[24]), 128);
        device = get_av_log_device_info();
        av_log_ext(NULL, AV_LOG_ERROR, "{\"device\":\"%s\",\"filename\":\"%s\",\"time\":\"%lld\",\"delay\":\"%lld\",\"from\":\"%s\"}\n", 
                device,
                h->filename, 
                av_gettime(), 
                delay,
                fname);

        
        return SMT_STATUS_INVALID_INPUT;
    }
#endif

    if(!recv->stack_init){
        recv->stack_init = true;
        memset(recv->mpu_head, 0, sizeof(smt_packet *)*10);
    }
    if(recv->packet_parser_status == SMT_STATUS_NEED_MORE_DATA){
        packet = recv->current_packet;
    }else{
        recv->packet_parse_phase = SMT_PARSE_NOT_START;
        if(recv->packet_parser_status == SMT_STATUS_HAS_MORE_DATA)
            recv->packet_buffer_data_len = recv->has_more_data;
        else
            recv->packet_buffer_data_len = 0;
        recv->process_position = 0;
        packet = (smt_packet *)av_mallocz(sizeof(smt_packet));
    }
    
    status = smt_parse_packet(h, recv, buffer, size, packet);
    switch(status){
        case SMT_STATUS_OK:
            recv->packet_parse_phase = SMT_PARSE_NOT_START;
            status = smt_add_packet(h, recv, packet);
            break;
        case SMT_STATUS_HAS_MORE_DATA:
            status = smt_add_packet(h, recv, packet);
            break;
        case SMT_STATUS_NEED_MORE_DATA:
            *p_size = recv->need_more_data;
            recv->current_packet = packet;
            break;
        case SMT_STATUS_NOT_SUPPORT:
        case SMT_STATUS_ERROR:
            if(recv->packet_parse_phase == SMT_ALLOC_PAYLOAD_DATA){
                switch(packet->type){
                    case mpu_payload:{
                        smt_payload_mpu *mpu = (smt_payload_mpu *)(&packet->payload);
                        payload_data = mpu->data;
                        break;
                    }case gfd_payload:{
                        smt_payload_gfd *gfd = (smt_payload_gfd *)(&packet->payload);
                        payload_data = gfd->data;
                        break;
                    }case sig_payload:{
                        smt_payload_sig *sig = (smt_payload_sig *)(&packet->payload);
                        payload_data = sig->data;
                        break;
                    }case repair_symbol_payload:{
                        smt_payload_id *id = (smt_payload_id *)(&packet->payload);
                        payload_data = id->data;
                        break;
                    }    
                }
            }
            if(payload_data)
                av_freep(&payload_data);

            av_freep(&packet);
            break;
    }
    return status;
}

void smt_release_mpu(URLContext *h, smt_mpu *mpu)
{
    //int i = 0;
    if(!mpu)
        return;
    if(mpu->mpu_header_data)
        av_freep(&mpu->mpu_header_data);

    if(mpu->moof_header_data)
        av_freep(&mpu->moof_header_data);

    if(mpu->sample_data)
        av_freep(&mpu->sample_data);

/*
    if(!mpu->sample)
        return;
    for(;i<mpu->sample_size;i++){
        if(mpu->sample[i].sample_data)
            free(mpu->sample[i].sample_data);
    }
    av_freep(&mpu->sample);
*/
    av_freep(&mpu);
}
#ifdef SMT_NET_STATE
#define SERVER_NAME_LEN (127)
unsigned char  net_state_buffer[MTU] = {0};
int smt_pack_net_state(unsigned char** outbuffer) {
    unsigned char* buffer = net_state_buffer;
    *outbuffer = net_state_buffer;

	smt_payload_netstate *pld = NULL;
    smt_packet *pkt = NULL;
    int64_t now_time = av_gettime();


	//initialization
	pkt = (smt_packet *)av_mallocz(sizeof(smt_packet));
	pkt->V = 0;
	pkt->C = 1;
	pkt->FEC = no_fec;
	pkt->r = 0;
	pkt->X = 0;
	pkt->R = 0;
	pkt->RES = 0;
	pkt->type = net_state;
    pkt->packet_id = 8;
    pld = (smt_payload_netstate *)&(pkt->payload);
    pld->delivery_time = now_time;
    
    memset(buffer, 0, sizeof(unsigned char) * MTU);
    buffer[0] = (pkt->R&0x01) | (pkt->X&0x01) << 1 | (pkt->r&0x01) << 2 | (pkt->FEC&0x03) << 3 | (pkt->C&0x01) << 5 | (pkt->V&0x03) << 6;
    buffer[1] = (pkt->type&0x3f) | (pkt->RES&0x03)<<6;
	buffer[2] = (unsigned char)(pkt->packet_id >> 8);
	buffer[3] = (unsigned char)(pkt->packet_id);
	buffer[4] = (unsigned char)(pkt->timestamp >> 24);
	buffer[5] = (unsigned char)(pkt->timestamp >> 16); 
	buffer[6] = (unsigned char)(pkt->timestamp >> 8); 
	buffer[7] = (unsigned char)(pkt->timestamp);
	buffer[8] = (unsigned char)(pkt->packet_sequence_number >> 24);
	buffer[9] = (unsigned char)(pkt->packet_sequence_number >> 16); 
	buffer[10] = (unsigned char)(pkt->packet_sequence_number >> 8); 
	buffer[11] = (unsigned char)(pkt->packet_sequence_number);
	buffer[12] = (unsigned char)(pkt->packet_counter >> 24);
	buffer[13] = (unsigned char)(pkt->packet_counter >> 16); 
	buffer[14] = (unsigned char)(pkt->packet_counter >> 8); 
	buffer[15] = (unsigned char)(pkt->packet_counter);

    buffer[16] = (unsigned char)((0xff00000000000000 & pld->delivery_time) >> 56);
    buffer[17] = (unsigned char)((0x00ff000000000000 & pld->delivery_time) >> 48);
    buffer[18] = (unsigned char)((0x0000ff0000000000 & pld->delivery_time) >> 40);
    buffer[19] = (unsigned char)((0x000000ff00000000 & pld->delivery_time) >> 32);
    buffer[20] = (unsigned char)((0x00000000ff000000 & pld->delivery_time) >> 24);
    buffer[21] = (unsigned char)((0x0000000000ff0000 & pld->delivery_time) >> 16);
    buffer[22] = (unsigned char)((0x000000000000ff00 & pld->delivery_time) >> 8);
    buffer[23] = (unsigned char)((0x00000000000000ff & pld->delivery_time) );
    
    char* device_info = get_av_log_device_info();
    int   device_info_len = strlen(device_info) > SERVER_NAME_LEN?SERVER_NAME_LEN+1:strlen(device_info)+1 ;
    memcpy(&(buffer[24]), device_info, device_info_len);
    av_free(pkt);
    return SERVER_NAME_LEN+24;
}
#endif



smt_status smt_pack_mpu(URLContext *h, smt_send_entity *snd, unsigned char* buffer, int length)
{
	int position = 0;
	smt_packet *pkt = NULL;
	smt_payload_mpu *pld = NULL;
	int size, offset = 0, ext = 0;
    smt_status status;
    smt_fragment_type FT = none;
	
    if(length >= MIN_PACKET_SIZE){
        unsigned char *tag = buffer+4;
        int type = MKTAG(tag[0],tag[1],tag[2],tag[3]);
        if(type == MKTAG('f','t','y','p')){
            int hdlr_offset, mmpu_offset;
            unsigned char *hdlr, *mmpu;
            int media;
            hdlr_offset = smt_find_field(buffer,length,"hdlr", 4);
            if(hdlr_offset < 0)
                return SMT_STATUS_NEED_MORE_DATA;
            hdlr = buffer + hdlr_offset;
            media = MKTAG((hdlr + 0x0c)[0], (hdlr + 0x0c)[1], (hdlr + 0x0c)[2], (hdlr + 0x0c)[3]);
            if(media == MKTAG('v','i','d','e'))
                snd->asset = 1;
            else if(media == MKTAG('s','o','u','n'))
                snd->asset = 0;
            mmpu_offset = buffer[0] << 24 | buffer[1] << 16 | buffer[2] << 8 | buffer[3];
            mmpu = buffer + mmpu_offset;
            snd->mpu_seq[snd->asset] = (mmpu + 0x0d)[0] << 24 | (mmpu + 0x0d)[1] << 16 | (mmpu + 0x0d)[2] << 8 | (mmpu + 0x0d)[3];
            snd->moof_index = -1;
            snd->sample_index = 0;
            FT = mpu_metadata;
        }else if(type == MKTAG('m','o','o','f')){
            snd->moof_index++;
            FT = movie_fragment_metadata;
        }else{
            snd->sample_index++;
            FT = mfu;
        }
    }else{
        snd->sample_index++;
        FT = mfu;
        av_log(h, AV_LOG_WARNING, "packet length is %d, assume it is mfu. time = %llu\n", length, av_gettime());
    }

	//initialization
	pkt = (smt_packet *)av_mallocz(sizeof(smt_packet));
	pkt->V = 0;
	pkt->C = 1;
	pkt->FEC = no_fec;
	pkt->r = 0;
	pkt->X = 0;
	pkt->R = 0;
	pkt->RES = 0;
	pkt->type = mpu_payload;
    pkt->packet_id = snd->asset;
    pld = (smt_payload_mpu *)&(pkt->payload);
	pld->T = 1;
	pld->A = 0;
	pld->DU_Header[0].priority = 0;
	pld->DU_Header[0].dep_counter = 0;
    pld->data = (unsigned char *)av_mallocz(MTU);
	pld->MPU_sequence_number = snd->mpu_seq[snd->asset];
	pld->DU_Header[0].movie_fragment_sequence_number = snd->moof_index;
	pld->DU_Header[0].sample_number = snd->sample_index;
    pld->FT = FT;
    
    offset = SMT_PACKET_HEAD_LENGTH + SMT_MPU_PAYLOAD_HEAD_LENGTH;
	if(pld->FT == mfu)
		offset += SMT_MPU_PAYLOAD_DU_HEAD_LENGTH;
    if(pkt->X){
        ext = pkt->header_extension.length + 4;
        offset += ext;
        if(offset >= MTU)
        av_log(h, AV_LOG_ERROR, "UDP MTU is less than SMT header, program crash.\n");
        av_assert0(offset < MTU);
    }
    size = MTU - offset;
	if(length%size == 0)
		pld->frag_counter = length/size;
	else
		pld->frag_counter = length/size + 1;
	if(size >= length)
		pld->f_i = complete_data;
	else
		pld->f_i = first_fragment;
	while(position < length){
        memset(pld->data, 0, MTU);
		pkt->packet_counter = snd->pkt_counter;
        pkt->packet_sequence_number = snd->pkt_seq[snd->asset];
        int64_t now_time =  av_gettime();
        if(0 != diff_time) {
            time_t timer = NULL;
            time(&timer);
            struct tm today_zero_time = *localtime(&timer);
            today_zero_time.tm_hour = 0;
            today_zero_time.tm_min  = 0;
            today_zero_time.tm_sec  = 0;
            time_t timep = mktime(&today_zero_time);
            ffmpeg_begin_time = now_time /1000 - timep * 1000 + diff_time; 
            diff_time = 0;
        }
        static int64_t first_time = 0;
        if( 0 == first_time ) {
            first_time = now_time;
        }
        pkt->timestamp =  ffmpeg_begin_time + (now_time - first_time)/1000 ;
		smt_assemble_packet_header(h, snd, pld->data, pkt);

        if(position > 0){
            if(length - position <= size)
                pld->f_i = last_fragment;
            else
                pld->f_i = middle_fragment;
        }
    
		if(pld->FT == mfu)
			pld->DU_Header[0].offset = position;

		if(pld->f_i == complete_data || pld->f_i == last_fragment){
			pld->length = length - position + SMT_MPU_PAYLOAD_HEAD_LENGTH - 2 + (pld->FT == mfu?SMT_MPU_PAYLOAD_DU_HEAD_LENGTH:0);
            pld->data_len = length - position;
		}else{
			pld->length = MTU - SMT_PACKET_HEAD_LENGTH - 2;
            pld->data_len = size;
        }

        av_assert0(pld->data_len != 2 || pld->f_i != complete_data || pld->FT == mfu);
        
        smt_assemble_payload_header(h, pld->data + SMT_PACKET_HEAD_LENGTH + ext , pld);
        memcpy(pld->data + offset, buffer + position, pld->data_len);

        //send data
#ifdef FIXED_UDP_LEN
        if(0 > smt_callback_entity.packet_send(h, pld->data, MTU))
#else
        if(0 > smt_callback_entity.packet_send(h, pld->data, pld->data_len + offset))
#endif
        {   
            av_log(h, AV_LOG_ERROR, "send smt packet failed.\n");
            status = SMT_STATUS_ERROR;
            break;
        }

/*
        av_log(h, AV_LOG_WARNING, "packet send len = %d. asset id = %d, seq = %d, mpu = %d, counter =%d, timestamp = %d, offset = %d\n",
            pld->data_len + offset,
            pkt->packet_id, 
            pkt->packet_sequence_number, 
            pld->MPU_sequence_number,
            pkt->packet_counter,
            pkt->timestamp,
            offset);
*/   
		position += pld->data_len;
        snd->pkt_seq[snd->asset]++;
	}
    av_freep(&pld->data);
    av_freep(&pkt);
	return SMT_STATUS_OK;
}

#ifdef SMT_PROTOCAL_SIGNAL
smt_status smt_pack_signal(URLContext *h)
{
    generate_and_send_signal(h);
    return SMT_STATUS_OK;
}
#endif


