##############################################################################
# Title        : AVR Makefile for Windows
#
# Created      : Matthew Millman 2018-05-29
#                http://tech.mattmillman.com/
#
##############################################################################

# Fixes clash between windows and coreutils mkdir. Comment out the below line to compile on Linux
COREUTILS  = C:/Projects/coreutils/bin/

DEVICE     = atmega32u4
CLOCK      = 16000000
PROGRAMMER = -c arduino -P COM13 -c avr109 -b 57600 
SRCS       = main.c config.c util.c timeout.c timer.c sms.c usart_buffered.c i2c.c spi.c adc.c sc16is7xx.c ds2482.c ds18x20.c gsm.c smshistory.c crc8.c
OBJS       = $(SRCS:.c=.o)
FUSES      = -U lfuse:w:0x4F:m -U hfuse:w:0xC1:m -U efuse:w:0xff:m
DEPDIR     = deps
DEPFLAGS   = -MT $@ -MMD -MP -MF $(DEPDIR)/$*.Td
RM         = rm
MV         = mv
MKDIR      = $(COREUTILS)mkdir

POSTCOMPILE = $(MV) $(DEPDIR)/$*.Td $(DEPDIR)/$*.d && touch $@

AVRDUDE = avrdude $(PROGRAMMER) -p $(DEVICE)
COMPILE = avr-gcc -Wall -Os $(DEPFLAGS) -DF_CPU=$(CLOCK) -mmcu=$(DEVICE)

all:	main.hex

.c.o:
	@$(MKDIR) -p $(DEPDIR)
	$(COMPILE) -c $< -o $@
	@$(POSTCOMPILE)

.S.o:
	@$(MKDIR) -p $(DEPDIR)
	$(COMPILE) -x assembler-with-cpp -c $< -o $@
	@$(POSTCOMPILE)

.c.s:
	@$(MKDIR) -p $(DEPDIR)
	$(COMPILE) -S $< -o $@
	@$(POSTCOMPILE)

flash:	all
	$(AVRDUDE) -U flash:w:main.hex:i

fuse:
	$(AVRDUDE) $(FUSES)

install: flash

clean:
	$(RM) -f main.hex main.elf $(OBJS)

main.elf: $(OBJS)
	$(COMPILE) -o main.elf $(OBJS)

main.hex: main.elf
	avr-objcopy -j .text -j .data -O ihex main.elf main.hex

disasm:	main.elf
	avr-objdump -d main.elf

cpp:
	$(COMPILE) -E $(SRCS)

$(DEPDIR)/%.d:
.PRECIOUS: $(DEPDIR)/%.d

include $(wildcard $(patsubst %,$(DEPDIR)/%.d,$(basename $(SRCS))))