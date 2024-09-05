/* lcd/plcd.c */

/* Copyright (c) 2024 Peter Welch
   All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are met:

   * Redistributions of source code must retain the above copyright
     notice, this list of conditions and the following disclaimer.
   * Redistributions in binary form must reproduce the above copyright
     notice, this list of conditions and the following disclaimer in
     the documentation and/or other materials provided with the
     distribution.
   * Neither the name of the copyright holders nor the names of
     contributors may be used to endorse or promote products derived
     from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.
*/

/* Parallel-interfaced LCD display driver.
 *
 * Datasheet: Hitachi HD44780U (LCD-II) ADE-207-272(Z) '99.9 Rev. 0.0
 *
 * A message interface to a Hitachi HD44780 connected to PORTB[0..7] 
 * and PORTC[0..3]. This communicates with the HD44780 in 8 bit mode.
 * The least significant three bits of PORTC provide RS, RW and E control
 * signals. The fourth bit controls the backlight.
 *
 * PORTD2 controls a switch that powers the display, where
 *     low = enabled
 *     high = disabled.
 * PORTC3 controls the lcd backlight, where
 *     low = off
 *     high = on.
 * PORTC2 E   - interface
 * PORTC1 R/W - interface
 * PORTC0 RS  - interface
 * Initialization is performed upon the receipt of the first job following
 * a reset.
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

#include "sys/ioctl.h"
#include "sys/defs.h"
#include "sys/msg.h"
#include "sys/clk.h"
#include "lcd/plcd.h"

/* I am .. */
#define SELF PLCD
#define this plcd

#define ONE_MILLISECOND       1
#define INITIAL_DELAY ONE_MILLISECOND

#define MAX_ADDRESS 103

/* Write Operation microsecond delays [p.52,55] */
#define tAS    0.040    /* address setup */
#define tAH    0.010    /* address hold */
#define PW_EH  0.230    /* Enable pulse width */

#define RS_BIT _BV(PORTC0) /* set = data reg, clear = instruction reg */
#define RW_BIT _BV(PORTC1) /* set = read, clear = write */
#define EN_BIT _BV(PORTC2) /* set = enable, clear = disable */
#define BL_BIT _BV(PORTC3) /* set = on, clear = off */

#define BF_BIT _BV(PCINT7) /* set = busy, clear = not busy */
#define PWR_BIT _BV(PORTD2) /* set = power off, clear = power on */

typedef enum {
    IDLE = 0,
    /* job execution */
    AWAITING_REPLY,
    /* initialization process with internal reset. [p.43-4] */
    REMOVING_POWER,
    APPLYING_POWER,
    SETTING_FUNCTION,
    SETTING_DISPLAY_CONTROL,
    CLEARING_DISPLAY,
    SETTING_ENTRY_MODE,
    RETURNING_HOME
} __attribute__ ((packed)) state_t;

typedef enum {
    INSTRUCTION_REGISTER = 0,
    DATA_REGISTER
} __attribute__ ((packed)) lcd_register;

typedef struct {
    state_t state;
    unsigned inited : 1;
    plcd_info *headp;
    union {
        clk_info clk;
    } info;
} plcd_t;

/* I have .. */
static plcd_t this;

/* I can .. */
PRIVATE void start_job(void);
PRIVATE void resume(void);
PRIVATE void write_byte(uchar_t ch, lcd_register reg);
PRIVATE void wait_until_not_busy(void);

PRIVATE void power_down_lcd(void);
PRIVATE void power_up_lcd(void);

PRIVATE void select_data_register(void);
PRIVATE void select_instruction_register(void);
PRIVATE void enable_lcd(void);
PRIVATE void disable_lcd(void);
PRIVATE void select_read_mode(void);
PRIVATE void select_write_mode(void);
PRIVATE void turn_backlight_on(void);
PRIVATE void turn_backlight_off(void);

PRIVATE void enable_pinchange_interrupt(void);
PRIVATE void clear_pending_interrupt(void);
PRIVATE void enable_not_busy_interrupt(void);
PRIVATE void disable_not_busy_interrupt(void);

PRIVATE void configure_data_port_input(void);
PRIVATE void out_port(uchar_t c);
PRIVATE void configure_control_port_output(void);
PRIVATE void configure_power_pin_output(void);

PRIVATE void write_instruction(uchar_t n);
PRIVATE void write_data(uchar_t c);

PRIVATE void set_function_eightbit_twoline(void);
PRIVATE void set_display_control_on(void);
PRIVATE void clear_display(void);
PRIVATE void set_entry_mode_left(void);
PRIVATE void return_home(void);

/* initialization */
PUBLIC void config_plcd(void)
{
    /* Enable pullups on PORTB[0..7] */
    configure_data_port_input();

    /* Set RW high then Configure PORTC[0..3] as output. */
    select_read_mode();
    configure_control_port_output();
    configure_power_pin_output();

    /* Enable Pin Change Interrupt 0, so that PCINT7 can be used to detect
     * the Busy Flag. PCINT7 is set and cleared in PCMSK0 as and when required.
     */
    enable_pinchange_interrupt();
}

PUBLIC uchar_t receive_plcd(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case ALARM:
    case REPLY_RESULT:
    case NOT_BUSY:
        if (this.state) {
            resume();
        } else if (this.headp) {
            send_REPLY_INFO(this.headp->replyTo, m_ptr->RESULT, this.headp);
            if ((this.headp = this.headp->nextp) != NULL) 
                start_job();
        }
        break;

    case JOB:
        {
            plcd_info *ip = m_ptr->INFO;
            ip->nextp = NULL;
            ip->replyTo = m_ptr->sender;
            if (!this.headp) {
                this.headp = ip;
                start_job();
            } else {
                plcd_info *tp;
                for (tp = this.headp; tp->nextp; tp = tp->nextp)
                    ;
                tp->nextp = ip;
            }
        }
        break;

    case SET_IOCTL:
        switch (m_ptr->IOCTL) {
        case SIOC_BACKLIGHT:
            switch (m_ptr->LCOUNT) {
            case 0:
                turn_backlight_off();
                break;
            case 1:
                turn_backlight_on();
                break;
            }
            break;

        case SIOC_PLCD_COMMAND:
            switch (m_ptr->LCOUNT) {
            case 0:
                clear_display();
                break;
            case 1:
                return_home();
                break;
            }
            break;
        }
        send_REPLY_RESULT(m_ptr->sender, EOK);
        break;

    default:
        return ENOMSG;
    }
    return EOK;
}

PRIVATE void start_job(void)
{
    if (!this.inited) {
        /* Initialize on first use. */
        this.state = REMOVING_POWER;
        power_down_lcd();
        sae_CLK_SET_ALARM(this.info.clk, INITIAL_DELAY);
    } else {
        if (this.headp->instr) {
            this.state = AWAITING_REPLY;
            write_instruction(this.headp->instr);
        } else if (this.headp->n) {
            this.state = AWAITING_REPLY;
            this.headp->n--;
            write_data(*this.headp->p++);
        }
    }
}

PRIVATE void resume(void)
{
    switch (this.state) {

    case IDLE:
        break;

    /* JOB execution */
    case AWAITING_REPLY:
        if (this.headp->n) {
            this.headp->n--;
            write_data(*this.headp->p++);
        } else {
            this.state = IDLE;
            send_REPLY_RESULT(SELF, EOK);
        }
        break;

    /* HD44780 initialization by internal reset */
    case REMOVING_POWER:
        this.state = APPLYING_POWER;
        power_up_lcd();
        wait_until_not_busy();
        break;

    case APPLYING_POWER:
        this.state = SETTING_FUNCTION;
        set_function_eightbit_twoline();
        break;

    case SETTING_FUNCTION:
        this.state = SETTING_DISPLAY_CONTROL;
        set_display_control_on();
        break;

    case SETTING_DISPLAY_CONTROL:
        this.state = CLEARING_DISPLAY;
        clear_display();
        break;

    case CLEARING_DISPLAY:
        this.state = SETTING_ENTRY_MODE;
        set_entry_mode_left();
        break;

    case SETTING_ENTRY_MODE:
        this.state = RETURNING_HOME;
        return_home();
        break;

    case RETURNING_HOME:
        this.inited = TRUE;
        this.state = IDLE;
        /* Pickup where we left off. */
        if (this.headp)
            start_job();
        break;
    }
}

PRIVATE void write_byte(uchar_t ch, lcd_register reg)
{
    /* Within this operation, two minimum time periods have to be met:-
     *   - Address set-up time (RS, RW to E) = 40ns.
     *   - Enable pulse width (high level) = 230ns.
     * [p.52,58]
     *
     * The out_port() macro should introduce more than 40ns.
     *
     * Sending the completion message should introduce more than the 230ns.
     *
     * The delay introduced by the return mechanism and the subsequent
     * handling of the completion message should exceed the Enable cycle
     * time of 500ns.
     */
    select_write_mode();
    switch (reg) {
    case DATA_REGISTER:
        select_data_register();
        break;
    case INSTRUCTION_REGISTER:
        select_instruction_register();
        break;
    }
    out_port(ch);
    _delay_us(tAS);
    enable_lcd();
    _delay_us(PW_EH);
    disable_lcd();
    _delay_us(tAH);
    wait_until_not_busy();
}

PRIVATE void wait_until_not_busy(void)
{
    /* First clear RS_BIT and set RW_BIT so that the minimum address set-up
     * delay is met. Then configure PORTB[0..7] to be pulled-up inputs,
     * clear any pending interrupt on pinchange 0, set PCINT7 in PCMSK0 to
     * enable monitoring of * the BF_BIT, and finally set EN_BIT to energise
     * the lcd data bus outputs. If the busy flag is already low, this last
     * operation will trigger an interrupt.
     */
    select_instruction_register();
    configure_data_port_input();
    select_read_mode();
    clear_pending_interrupt(); 
    enable_not_busy_interrupt();
    enable_lcd();
}

/*-------------------------------------------------------
  Handle an PCINT0 interrupt.
  This occurs when the LCD has signaled not busy. 
  Mask the interrupt.
  Although it is listed on [p.74] as VectorNo.4,
  this appears as <__vector_3>: in the .lst file.
-------------------------------------------------------*/
ISR(PCINT0_vect)
{
    /* The BUSY_FLAG has changed from high to low.
     * Disable further interrupts, disable the lcd and send a message.
     */
    disable_not_busy_interrupt();
    disable_lcd();
    send_NOT_BUSY(SELF);
}

PRIVATE void power_down_lcd(void)
{
    PORTD |= PWR_BIT;
}

PRIVATE void power_up_lcd(void)
{
    PORTD &= ~PWR_BIT;
}

PRIVATE void select_data_register(void)
{
    PORTC |= RS_BIT;
}

PRIVATE void select_instruction_register(void)
{
    PORTC &= ~RS_BIT;
}

PRIVATE void enable_lcd(void)
{
    PORTC |= EN_BIT;
}

PRIVATE void disable_lcd(void)
{
    PORTC &= ~EN_BIT;
}

PRIVATE void select_read_mode(void)
{
    PORTC |= RW_BIT;
}

PRIVATE void select_write_mode(void)
{
    PORTC &= ~RW_BIT;
}

PRIVATE void turn_backlight_on(void)
{
    PORTC |= BL_BIT;
}

PRIVATE void turn_backlight_off(void)
{
    PORTC &= ~BL_BIT;
}

PRIVATE void enable_pinchange_interrupt(void)
{
    PCICR |= _BV(PCIE0);
}

PRIVATE void clear_pending_interrupt(void)
{
    PCIFR |= _BV(PCIF0);
}

PRIVATE void enable_not_busy_interrupt(void)
{
    PCMSK0 |= BF_BIT;
}

PRIVATE void disable_not_busy_interrupt(void)
{
    PCMSK0 &= ~BF_BIT;
}

PRIVATE void configure_data_port_input(void)
{
    PORTB = 0xff;
    DDRB = 0x00;
}

PRIVATE void out_port(uchar_t c)
{
    PORTB = c;
    DDRB = 0xff;
}

PRIVATE void configure_control_port_output(void)
{
    DDRC |= BL_BIT | EN_BIT | RW_BIT | RS_BIT;
}

PRIVATE void configure_power_pin_output(void)
{
    PORTD |= PWR_BIT;
    DDRD |= PWR_BIT;
}

PRIVATE void write_instruction(uchar_t n)
{
    write_byte((n), INSTRUCTION_REGISTER);
}

PRIVATE void write_data(uchar_t c)
{
    write_byte((c), DATA_REGISTER);
}

PRIVATE void set_function_eightbit_twoline(void)
{
    write_instruction(LCD_FUNCTION_SET | LCD_8BIT_MODE | LCD_2LINE);
}

PRIVATE void set_display_control_on(void)
{
    write_instruction(LCD_DISPLAY_CONTROL | LCD_DISPLAY_ON);
}

PRIVATE void clear_display(void)
{
    write_instruction(LCD_CLEAR_DISPLAY);
}

PRIVATE void set_entry_mode_left(void)
{
    write_instruction(LCD_ENTRY_MODE_SET | LCD_ENTRY_LEFT);
}

PRIVATE void return_home(void)
{
    write_instruction(LCD_RETURN_HOME);
}

/* end code */
