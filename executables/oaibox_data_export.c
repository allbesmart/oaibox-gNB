//
// Created by user on 14/10/21.
//
#ifndef OAIBOX_DATA_EXPORT
#define OAIBOX_DATA_EXPORT

// #define OAIBOX_DEBUG

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include "common/ran_context.h"
#include <softmodem-common.h>
#include "LAYER2/NR_MAC_gNB/mac_proto.h"
#include "openair2/RRC/NR/rrc_gNB_UE_context.h"
#include "MQTTClient.h"

// MQTT DEFINES
#define ADDRESS "wss://rabbit.oaibox.com/ws"
#define QOS 1
#define UPDATE_INTERVAL 1 // Seconds

char client_uuid[41];
char topic[128];

#define DATA_EXPORT_BUFFER_SIZE 8094
char buffer[DATA_EXPORT_BUFFER_SIZE];

volatile MQTTClient_deliveryToken deliveredtoken;

void delivered(void *context, MQTTClient_deliveryToken dt)
{
#ifdef OAIBOX_DEBUG
  printf("OAIBOX Message with token value %d delivery confirmed\n", dt);
#endif
  deliveredtoken = dt;
}

int msgarrvd(void *context, char *topicName, int topicLen, MQTTClient_message *message)
{
  int i;
  char *payloadptr;
  printf("Message arrived\n");
  printf("     topic: %s\n", topicName);
  printf("   message: ");
  payloadptr = message->payload;
  for (i = 0; i < message->payloadlen; i++) {
    putchar(*payloadptr++);
  }
  putchar('\n');
  MQTTClient_freeMessage(&message);
  MQTTClient_free(topicName);
  return 1;
}

void connlost(void *context, char *cause)
{
  printf("\nOAIBOX Connection lost with cause: %s\n", cause);
}

void gen_uuid(char *buf)
{
  struct timeval tp_cur;
  gettimeofday(&tp_cur, NULL);
  srand(tp_cur.tv_usec);

  char v[] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
  // 3fb17ebc-bc38-4939-bc8b-74f2443281d4
  // oai dash 8 dash 4 dash 4 dash 4 dash 12
  sprintf(buf, "oai-");

  // gen random for all spaces because lazy
  for (int i = 4; i < 40; ++i) {
    buf[i] = v[rand() % 16];
  }

  // put dashes in place
  buf[12] = '-';
  buf[17] = '-';
  buf[22] = '-';
  buf[27] = '-';

  // needs end byte
  buf[40] = '\0';
}

int get_tv_cur_minus_given(struct timeval *tv, struct timeval *tp_given, int *sign)
{
  struct timeval tp_cur;
  gettimeofday(&tp_cur, NULL);

  tv->tv_sec = tp_cur.tv_sec - tp_given->tv_sec;
  tv->tv_usec = tp_cur.tv_usec - tp_given->tv_usec;

  if (tv->tv_sec > 0) {
    *sign = 1;
    if (tv->tv_usec < 0) {
      tv->tv_sec--;
      tv->tv_usec = 1000000 + tv->tv_usec;
    }
  } else {
    if (tv->tv_sec == 0) {
      if (tv->tv_usec == 0) {
        *sign = 0;
      } else {
        if (tv->tv_usec < 0) {
          *sign = -1;
          tv->tv_usec *= -1;
        } else {
          *sign = 1;
        }
      }
    } else {
      *sign = -1;
      if (tv->tv_usec > 0) {
        tv->tv_sec++;
        tv->tv_usec = 1000000 - tv->tv_usec;
      } else {
        if (tv->tv_usec < 0)
          tv->tv_usec *= -1;
      }
    }
  }
  return 0;
}

void *oaibox_data_export_func()
{
  struct timeval tv_last_run;
  struct timeval tv_diff;
  int sign;
  uint64_t time = 0;

  // Get tenantId
  sprintf(topic, "telemetry.gNB.%s", get_softmodem_params()->tenantId);

  // Generate a random client_uuid
  gen_uuid(client_uuid);
#ifdef OAIBOX_DEBUG
  printf("OAIBOX client_id: %s\n", client_uuid);
#endif

  if (strlen(topic) <= 20) {
    usleep(100000);
    printf("OAIBOX error parsing tenantId\n");
    exit(-1);
  }

  // Connect to MQTT broker
  MQTTClient client;
  MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer_ws;
  MQTTClient_SSLOptions ssl_opts = MQTTClient_SSLOptions_initializer;
  MQTTClient_message pubmsg = MQTTClient_message_initializer;
  MQTTClient_deliveryToken token;
  int rc;
  MQTTClient_create(&client, ADDRESS, client_uuid, MQTTCLIENT_PERSISTENCE_NONE, NULL);
  conn_opts.username = "dashboard";
  conn_opts.password = "dashboard";
  conn_opts.ssl = &ssl_opts;
  conn_opts.keepAliveInterval = 20;
  conn_opts.cleansession = 1;

  MQTTClient_setCallbacks(client, NULL, connlost, msgarrvd, delivered);
  if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
    printf("OAIBOX Failed to connect, return code %d\n", rc);
    exit(EXIT_FAILURE);
  }

  while (!oai_exit) {
    // Get useconds until the next run and usleep for that amount of time
    get_tv_cur_minus_given(&tv_diff, &tv_last_run, &sign);
    int useconds = (UPDATE_INTERVAL * 1000000) - (tv_diff.tv_sec * 1000000 + tv_diff.tv_usec);
    if (useconds > 0.0 && useconds < 30000000) {
      usleep(useconds);
    }

    // Update timestamp
    gettimeofday(&tv_last_run, NULL);
    time = tv_last_run.tv_sec * (uint64_t)1000000 + tv_last_run.tv_usec;

    sprintf(buffer,
            "{\"id\": %ld, "
            "\"frame\": %d, "
            "\"slot\": %d, "
            "\"pci\": %d, "
            "\"ues\": [",
            RC.nrrrc[0]->nr_cellid,
            RC.gNB[0]->UL_INFO.frame,
            RC.gNB[0]->UL_INFO.slot,
            RC.nrmac[0]->common_channels[0].physCellId);

    int ue_counter = 0;

    UE_iterator(RC.nrmac[0]->UE_info.list, UE)
    {
      if (ue_counter > 0) {
        sprintf(&buffer[strlen(buffer)], ", ");
      }
      ue_counter++;

      NR_mac_stats_t *mac_stats = &UE->mac_stats;
      long rsrp = mac_stats->num_rsrp_meas > 0 ? mac_stats->cumul_rsrp / mac_stats->num_rsrp_meas : 0;
      double rsrq = NAN;
      double sinr = NAN;
      float pucch_snr = (float)UE->UE_sched_ctrl.pucch_snrx10 / 10.0;
      float pusch_snr = (float)UE->UE_sched_ctrl.pusch_snrx10 / 10.0;
      float rssi = (float)UE->UE_sched_ctrl.raw_rssi / 10.0;
      uint8_t cqi = (UE->UE_sched_ctrl.pucch_snrx10 + 640) / 5.0 / 10.0;
      if (cqi > 15) {
        cqi = 15;
      }

      // RRC Measurements values
      struct rrc_gNB_ue_context_s *ue_context_p = rrc_gNB_get_ue_context(RC.nrrrc[0], UE->rnti);
      if (ue_context_p && ue_context_p->ue_context.measResults) {
        if (ue_context_p->ue_context.measResults->measResultServingMOList.list.count > 1)
          LOG_W(RRC, "Received %d MeasResultServMO, but handling only 1!\n", ue_context_p->ue_context.measResults->measResultServingMOList.list.count);

        NR_MeasResultServMO_t *measresultservmo = ue_context_p->ue_context.measResults->measResultServingMOList.list.array[0];
        NR_MeasResultNR_t *measresultnr = &measresultservmo->measResultServingCell;
        NR_MeasQuantityResults_t *mqr = measresultnr->measResult.cellResults.resultsSSB_Cell;

        if (mqr != NULL) {
          rsrp = *mqr->rsrp - 156;
          rsrq = (float)(*mqr->rsrq - 87) / 2.0f;
          sinr = (float)(*mqr->sinr - 46) / 2.0f;
        }
      }

      sprintf(&buffer[strlen(buffer)],
              "{"
              "\"rnti\": \"%04x\", "
              "\"dlBytes\": %" PRIu64
              ", "
              "\"dlMcs\": %d, "
              "\"dlBler\": %f, "
              "\"ulBytes\": %" PRIu64
              ", "
              "\"ulMcs\": %d, "
              "\"ulBler\": %f, "
              "\"ri\": %d, "
              "\"pmi\": \"(%d,%d)\", ",
              UE->rnti,
              UE->mac_stats.dl.total_bytes, // sum byte values from all LCIDs
              UE->UE_sched_ctrl.dl_bler_stats.mcs,
              UE->UE_sched_ctrl.dl_bler_stats.bler,
              UE->mac_stats.ul.total_bytes,
              UE->UE_sched_ctrl.ul_bler_stats.mcs,
              UE->UE_sched_ctrl.ul_bler_stats.bler,
              UE->UE_sched_ctrl.CSI_report.cri_ri_li_pmi_cqi_report.ri + 1,
              UE->UE_sched_ctrl.CSI_report.cri_ri_li_pmi_cqi_report.pmi_x1,
              UE->UE_sched_ctrl.CSI_report.cri_ri_li_pmi_cqi_report.pmi_x2);

      if (UE->UE_sched_ctrl.CSI_report.cri_ri_li_pmi_cqi_report.wb_cqi_1tb != 0 ) {
        cqi = UE->UE_sched_ctrl.CSI_report.cri_ri_li_pmi_cqi_report.wb_cqi_1tb;
      }
      if (!isnan(rsrq)) {
        sprintf(&buffer[strlen(buffer)], "\"rsrq\": %.1f, ", rsrq);
      }
      if (!isnan(sinr)) {
        sprintf(&buffer[strlen(buffer)], "\"sinr\": %.1f, ", sinr);
      }

      sprintf(&buffer[strlen(buffer)],
              "\"rsrp\": %ld, "
              "\"rssi\": %.1f, "
              "\"cqi\": %d, "
              "\"pucchSnr\": %.1f, "
              "\"puschSnr\": %.1f}",
              rsrp,
              rssi,
              cqi,
              pucch_snr,
              pusch_snr);
    }
    sprintf(&buffer[strlen(buffer)], "], \"timestamp\": %ju}", time);

    pubmsg.payload = buffer;
    pubmsg.payloadlen = strlen(buffer);
    pubmsg.qos = QOS;
    pubmsg.retained = 0;
    deliveredtoken = 0;
    MQTTClient_publishMessage(client, topic, &pubmsg, &token);

#ifdef OAIBOX_DEBUG
    printf("OAIBOX Waiting for publication of message:\n%s\nOn topic: %s for client with ClientID: %s\n", buffer, topic, client_uuid);
#endif

    while (deliveredtoken != token) {
      usleep(10000);
    }

    // Add an exit condition
    if (buffer[0] == 0) {
      printf("OAIBOX Client exiting....");
      break;
    }
  }

  // Close the connection to MQTT broker
  MQTTClient_disconnect(client, 10000);
  MQTTClient_destroy(&client);

  return NULL;
}

#endif