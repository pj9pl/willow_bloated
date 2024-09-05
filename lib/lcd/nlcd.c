/* lcd/nlcd.c */

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

/* A message interface to a Hitachi HD44780 at i2c address 0x4E.
 * This uses the TWI interface to communicate with a PCF8574 i2c
 * expander which writes to the HD44780 in 4 bit mode. The PCF8574
 * cannot read the HD44780.
 * The least significant nibble of the expander output provide the
 * RS, RW and EN control signals to the HD44780. The fourth bit
 * controls the backlight. The most significant nibble carries the
 * data appropriate for the current writing phase.
 *
 * Writing a byte to the HD44780 involves a sequence of ten phases
 * which bit-bang the byte into the HD44780.
 *
 * Initialization is performed upon the receipt of the first job
 * following a reset. Considering that the TWI frequency is 100kHz,
 * the 150us delay is unecessary.
 */

#include "sys/ioctl.h"
#include "sys/defs.h"
#include "sys/msg.h"
#include "sys/clk.h"
#include "net/twi.h"
#include "lcd/nlcd.h"

/* I am .. */
#define SELF NLCD
#define this nlcd

/* the slave address of the PCF8574 i2c expander */
#define LCD_I2C_ADDRESS    0x4E

/* These values are presented to the HD44780 during
 * initialization in the selection of the interface bus width.
 * They are (LCD_FUNCTION_SET | LCD_8BIT_MODE) and 
 * (LCD_FUNCTION_SET | LCD_4BIT_MODE) commands, prior to their
 * being shifted left 4 bits by the write4bits() function.
 */
#define EIGHT_BIT          0x03 /* (LCD_FUNCTION_SET | LCD_8BIT_MODE) >> 4 */
#define FOUR_BIT           0x02 /* (LCD_FUNCTION_SET | LCD_4BIT_MODE) >> 4 */

#define FIFTY_MILLISECONDS   50
#define FIFTEEN_MILLISECONDS 15
#define FIVE_MILLISECONDS     5
#define ONE_MILLISECOND       1
#define ONE_SECOND         1000
#define TWO_SECONDS        2000

#define INITIAL_DELAY FIFTY_MILLISECONDS
#define SET_BACKLIGHT_DELAY ONE_SECOND
#define FIRST_DELAY FIVE_MILLISECONDS
#define SECOND_DELAY FIVE_MILLISECONDS
#define THIRD_DELAY ONE_MILLISECOND
#define CLEAR_DELAY TWO_SECONDS
#define HOME_DELAY TWO_SECONDS
#define HIGH_HOLD_DELAY ONE_MILLISECOND
#define LOW_HOLD_DELAY ONE_MILLISECOND

/* These next defines were adopted from
 * fdebrabander's LiquidCrystal_I2C github repo.
 */
// commands
#define LCD_CLEAR_DISPLAY   0x01
#define LCD_RETURN_HOME     0x02
#define LCD_ENTRY_MODE_SET  0x04
#define LCD_DISPLAY_CONTROL 0x08
#define LCD_CURSOR_SHIFT    0x10
#define LCD_FUNCTION_SET    0x20
#define LCD_SET_CGRAM_ADDR  0x40
#define LCD_SET_DDRAM_ADDR  0x80

// flags for display entry mode
#define LCD_ENTRY_RIGHT            0x00
#define LCD_ENTRY_LEFT             0x02
#define LCD_ENTRY_SHIFT_INCREMENT  0x01
#define LCD_ENTRY_SHIFT_DECREMENT  0x00

// flags for display on/off control
#define LCD_DISPLAY_ON     0x04
#define LCD_DISPLAY_OFF    0x00
#define LCD_CURSOR_ON      0x02
#define LCD_CURSOR_OFF     0x00
#define LCD_BLINK_ON       0x01
#define LCD_BLINK_OFF      0x00

// flags for display/cursor shift
#define LCD_DISPLAY_MOVE   0x08
#define LCD_CURSOR_MOVE    0x00
#define LCD_MOVE_RIGHT     0x04
#define LCD_MOVE_LEFT      0x00

// flags for function set
#define LCD_8BIT_MODE      0x10
#define LCD_4BIT_MODE      0x00
#define LCD_2LINE          0x08
#define LCD_1LINE          0x00
#define LCD_5x10_DOTS      0x04
#define LCD_5x8_DOTS       0x00

/* end of fdebrabander's defines */

/* The byte sent to the PCF 8574 i2c uses
 * the low nibble to apply control signals.
 */
#define RS_BIT 0x01 /* set = data register, clear = instruction register */
#define RW_BIT 0x02 /* set = read, clear = write */
#define EN_BIT 0x04 /* set = ENABLE HIGH, clear ENABLE LOW */
#define BL_BIT 0x08 /* set = on, clear = off */

#define DATA_MASK          0xF0 
#define CONTROL_MASK       0x0F

#define HIGH_NIBBLE(n)     ((n) & DATA_MASK)
#define LOW_NIBBLE(n)      ((n) << 4 & DATA_MASK)

#define HL_NIBBLE(n)       (this.highNibble ? HIGH_NIBBLE(n) : LOW_NIBBLE(n))

#define OFF FALSE
#define ON  TRUE

typedef enum {
    IDLE = 0,
    /* initialization process */
    IN_INITIAL_DELAY,
    SETTING_BACKLIGHT,
    IN_SET_BACKLIGHT_DELAY,
    AT_FIRST_ATTEMPT,
    IN_FIRST_DELAY,
    AT_SECOND_ATTEMPT,
    IN_SECOND_DELAY,
    AT_THIRD_ATTEMPT,
    IN_THIRD_DELAY,
    AT_FOURTH_ATTEMPT,
    SETTING_FUNCTION,
    SETTING_DISPLAY_CONTROL,
    CLEARING_DISPLAY,
    IN_CLEAR_DELAY,
    SETTING_ENTRY_MODE,
    GOING_HOME,
    IN_HOME_DELAY,
    /* job execution */
    AWAITING_REPLY
} __attribute__ ((packed)) state_t;

/* These are the phases of sending a byte as two nibbles. */
typedef enum {
    NIL_PHASE = 0,
    WR_NIBBLE,
    EN_HIGH,
    EN_HIGH_HOLD,
    EN_LOW,
    EN_LOW_HOLD
} __attribute__ ((packed)) nlcd_phase;

/* I have .. */
typedef struct {
    state_t state;
    unsigned init_ok : 1;
    unsigned backLight : 1;
    unsigned dataRegister : 1;
    unsigned writeMode : 1;
    unsigned highNibble : 1;
    nlcd_info *headp;
    nlcd_phase phase;
    uchar_t data;
    uchar_t displayFunction;
    uchar_t displayControl;
    uchar_t displayMode;
    union {
        twi_info twi;
        clk_info clk;
    } info;
} nlcd_t;

static nlcd_t this;

/* I can .. */
PRIVATE void start_job(void);
PRIVATE void resume(void);
PRIVATE void do_init(void);
PRIVATE void write8bits(uchar_t c, bool_t dataReg);
PRIVATE void write4bits(uchar_t c);
PRIVATE void do_write(void);
PRIVATE void send_byte(uchar_t c);

#define bcmd(n) (write8bits((n), FALSE))
#define bputc(c) (write8bits((c), TRUE))

PUBLIC uchar_t receive_nlcd(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case ALARM:
    case REPLY_INFO:
    case REPLY_RESULT:
        if (this.phase) {
            do_write();
        } else if (this.state) {
            resume();
        } else if (this.headp) {
            send_REPLY_INFO(this.headp->replyTo, m_ptr->RESULT, this.headp);
            if ((this.headp = this.headp->nextp) != NULL) 
                start_job();
        }
        break;

    case INIT_OK:
        if (this.headp)
            start_job();
        break;

    case JOB:
        {
            nlcd_info *ip = m_ptr->INFO;
            ip->nextp = NULL;
            ip->replyTo = m_ptr->sender;
            if (!this.headp) {
                this.headp = ip;
                start_job();
            } else {
                nlcd_info *tp;
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
                this.backLight = OFF;
                break;
            case 1:
                this.backLight = ON;
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
    /* whilst the guts are being decided,
     * this cap will at least reply to any clients who send jobs.
     */
    if (!this.init_ok) {
        do_init();
    } else {
        if (this.headp->n) {
            this.headp->n--;
            bputc(*this.headp->p++);
            this.state = AWAITING_REPLY;
        }
    }
}


PRIVATE void resume(void)
{
    switch (this.state) {

    case IDLE:
        break;

    case IN_INITIAL_DELAY:
        this.backLight = ON;
        send_byte(NIL);
        this.state = SETTING_BACKLIGHT;
        break;

    case SETTING_BACKLIGHT:
        sae_CLK_SET_ALARM(this.info.clk, SET_BACKLIGHT_DELAY);
        this.state = IN_SET_BACKLIGHT_DELAY;
        break;

    case IN_SET_BACKLIGHT_DELAY:
        write4bits(EIGHT_BIT);
        this.state = AT_FIRST_ATTEMPT;
        break;

    case AT_FIRST_ATTEMPT:
        sae_CLK_SET_ALARM(this.info.clk, FIRST_DELAY);
        this.state = IN_FIRST_DELAY;
        break;

    case IN_FIRST_DELAY:
        write4bits(EIGHT_BIT);
        this.state = AT_SECOND_ATTEMPT;
        break;

    case AT_SECOND_ATTEMPT:
        sae_CLK_SET_ALARM(this.info.clk, SECOND_DELAY);
        this.state = IN_SECOND_DELAY;
        break;

    case IN_SECOND_DELAY:
        write4bits(EIGHT_BIT);
        this.state = AT_THIRD_ATTEMPT;
        break;

    case AT_THIRD_ATTEMPT:
        sae_CLK_SET_ALARM(this.info.clk, THIRD_DELAY);
        this.state = IN_THIRD_DELAY;
        break;

    case IN_THIRD_DELAY:
        write4bits(FOUR_BIT);
        this.state = AT_FOURTH_ATTEMPT;
        break;

    case AT_FOURTH_ATTEMPT:
        this.displayFunction = LCD_4BIT_MODE | LCD_2LINE | LCD_5x8_DOTS;
        bcmd(LCD_FUNCTION_SET | this.displayFunction);
        this.state = SETTING_FUNCTION;
        break;

    case SETTING_FUNCTION:
        this.displayControl = LCD_DISPLAY_ON | LCD_CURSOR_OFF | LCD_BLINK_OFF;
        bcmd(LCD_DISPLAY_CONTROL | this.displayControl);
        this.state = SETTING_DISPLAY_CONTROL;
        break;

    case SETTING_DISPLAY_CONTROL:
        bcmd(LCD_CLEAR_DISPLAY);
        this.state = CLEARING_DISPLAY;
        break;

    case CLEARING_DISPLAY:
        sae_CLK_SET_ALARM(this.info.clk, CLEAR_DELAY);
        this.state = IN_CLEAR_DELAY;
        break;

    case IN_CLEAR_DELAY:
        this.displayMode = LCD_ENTRY_LEFT | LCD_ENTRY_SHIFT_DECREMENT; 
        bcmd(LCD_ENTRY_MODE_SET | this.displayMode);
        this.state = SETTING_ENTRY_MODE;
        break;

    case SETTING_ENTRY_MODE:
        bcmd(LCD_RETURN_HOME);
        this.state = GOING_HOME;
        break;

    case GOING_HOME:
        sae_CLK_SET_ALARM(this.info.clk, HOME_DELAY);
        this.state = IN_HOME_DELAY;
        break;

    case IN_HOME_DELAY:
        send_INIT_OK(SELF); 
        this.init_ok = TRUE;
        this.state = IDLE;
        break;

    /* JOB execution */
    case AWAITING_REPLY:
        if (this.headp->n) {
            this.headp->n--;
            bputc(*this.headp->p++);
        } else {
            this.state = IDLE;
            send_REPLY_RESULT(SELF, EOK);
        }
        break;
    }
}


PRIVATE void do_init(void)
{
    sae_CLK_SET_ALARM(this.info.clk, INITIAL_DELAY);
    this.state = IN_INITIAL_DELAY;
}


PRIVATE void write8bits(uchar_t c, bool_t dataReg)
{
    this.highNibble = TRUE; /* perform first and second pass */
    this.data = c;
    this.dataRegister = dataReg;
    this.phase = WR_NIBBLE;
    do_write();
}


PRIVATE void write4bits(uchar_t c)
{
    this.highNibble = FALSE; /* perform second pass only */
    this.data = c;
    this.dataRegister = 0;
    this.phase = WR_NIBBLE;
    do_write();
}


PRIVATE void do_write(void)
{
    switch (this.phase) {
    case NIL_PHASE:
        break;

    case WR_NIBBLE:
        send_byte(HL_NIBBLE(this.data));
        this.phase = EN_HIGH;
        break;

    case EN_HIGH:
        send_byte(HL_NIBBLE(this.data) | EN_BIT);
        this.phase = EN_HIGH_HOLD;
        break;

    case EN_HIGH_HOLD:
        sae_CLK_SET_ALARM(this.info.clk, HIGH_HOLD_DELAY);
        this.phase = EN_LOW;
        break;

    case EN_LOW:
        send_byte(HL_NIBBLE(this.data) & ~EN_BIT);
        this.phase = EN_LOW_HOLD;
        break;

    case EN_LOW_HOLD:
        sae_CLK_SET_ALARM(this.info.clk, LOW_HOLD_DELAY);
        this.phase = this.highNibble ? WR_NIBBLE : NIL_PHASE;
        this.highNibble = FALSE;
        break;
    }

}


PRIVATE void send_byte(uchar_t c)
{
    this.info.twi.dest_addr = LCD_I2C_ADDRESS;
    this.info.twi.tcnt = 0;
    this.info.twi.rcnt = 0;
    this.info.twi.mcmd = c | (this.backLight ? BL_BIT : 0x00)
                          | (this.dataRegister ? RS_BIT : 0x00)
                          | (this.writeMode ? RW_BIT : 0x00);
    send_JOB(TWI, &this.info.twi);
}

/* end code */
