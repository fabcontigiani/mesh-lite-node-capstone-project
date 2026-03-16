/*
 * ULP Program for PIR Sensor Monitoring
 * Monitors GPIO 12 (RTC_GPIO15) for motion detection
 * 
 * When PIR sensor triggers (HIGH), wakes up main CPU
 * Otherwise, continues in ultra-low power sleep
 */

#include "soc/rtc_cntl_reg.h"
#include "soc/rtc_io_reg.h"
#include "soc/soc_ulp.h"

    /* Data section - variables shared with main CPU */
    .bss

    /* Counter of wake-ups triggered by PIR */
    .global wakeup_counter
wakeup_counter:
    .long 0

    /* Flag indicating PIR was triggered (1 = triggered) */
    .global pir_triggered
pir_triggered:
    .long 0

    /* Text section - ULP program code */
    .text

    /* Entry point of ULP program */
    .global entry
entry:
    /* Read GPIO 12 state (mapped to RTC_GPIO15)
     * RTC_GPIO_IN_REG contains input values
     * Bit 15 corresponds to RTC_GPIO15 (GPIO12)
     */
    READ_RTC_REG(RTC_GPIO_IN_REG, RTC_GPIO_IN_NEXT_S + 15, 1)
    
    /* If GPIO is HIGH (PIR triggered), jump to wake_up routine
     * jumpr: jump if R0 >= 1 (i.e., GPIO is HIGH)
     */
    jumpr wake_up, 1, GE

    /* GPIO is LOW - PIR not triggered
     * Halt ULP and wait for next wake-up period
     */
    halt

wake_up:
    /* PIR sensor detected motion - prepare to wake main CPU */
    
    /* Set pir_triggered flag to 1 */
    move r3, pir_triggered
    move r2, 1
    st r2, r3, 0

    /* Increment wakeup counter */
    move r3, wakeup_counter
    ld r2, r3, 0
    add r2, r2, 1
    st r2, r3, 0

    /* Wake up main CPU */
    wake
    
    /* Halt ULP program */
    halt
