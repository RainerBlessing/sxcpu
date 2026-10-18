#ifndef CPU_PORTS_H_
#define CPU_PORTS_H_

/**
 * What the CPU needs from the outside world: the devices on the pins of the ports RA..RE
 * (a screen, a sound chip, a joystick, a RAM...). The CPU knows only this interface, so it
 * can be used without SDL or any other front end.
 *
 * The registers of the ports are plain memory of the CPU. A device that drives an input pin
 * puts the level into the bit of the register: that is what the program reads.
 */
class PortDevice
{
public:
	virtual ~PortDevice() {}

	/**
	 * An instruction wrote a port register: mov, clr, inc or dec on it, or setb/clrb of a bit
	 * that is an output. The value can be the one it had before. This is the call for the
	 * side effects of a write, e.g. a pulse on a clock pin.
	 * @param port 0..4 for RA..RE
	 * @param registers the five port registers RA..RE
	 * @param cycle the clock cycle at the end of the instruction (it runs over after about 27 s)
	 */
	virtual void portWritten(int port, unsigned char* registers, unsigned int cycle) = 0;

	/**
	 * Before an instruction: a port register has another value than before the instruction
	 * that ran last, whichever instruction (or device) changed it.
	 * @param port 0..4 for RA..RE
	 * @param registers the five port registers RA..RE
	 */
	virtual void portChanged(int port, unsigned char* registers)
	{
		(void) port;
		(void) registers;
	}
};

#endif /*CPU_PORTS_H_*/
