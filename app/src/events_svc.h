/*
 * Copyright (c) 2024 Tareq Mhisen
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_EVENT_SVC_H_
#define APP_EVENT_SVC_H_

#define EVENT_QUEUE_SIZE 10

enum event_type {
	EVENT_BLE_CONNECTED,
	EVENT_BLE_NOT_CONNECTED,
};

struct event {
	enum event_type type;
};

/**
 * @brief Get the text representation of an event
 *
 * @param type event type enum
 * @return char* NULL if error
 */
char *events_svc_type_to_text(enum event_type type);

/**
 * @brief Pushes an event to the message queue
 *
 * @param evt pointer to the event to be sent.
 * @return int
 */
int events_svc_send_event(struct event *evt);

/**
 * @brief Gets an event from the message queue
 *
 * @param evt pointer to the event to be received.
 * @return int
 */
int events_svc_get_event(struct event *evt);

#endif /* APP_EVENT_SVC_H_ */