#ifndef GEOPHYS_TRANSPORT_UART_H
#define GEOPHYS_TRANSPORT_UART_H

#include "transport.h"

struct platform_uart;

/**
 * @brief Adapt an initialized platform UART to the generic byte transport.
 *
 * The returned interface borrows the UART instance. Its owner must keep the
 * UART initialized for the full lifetime of the interface.
 */
transport_interface_t transport_uart_interface(struct platform_uart *uart);

#endif /* GEOPHYS_TRANSPORT_UART_H */
