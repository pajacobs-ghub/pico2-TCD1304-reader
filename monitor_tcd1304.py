# monitor_tcd1304.py
# Functions/procedure to talk to the RP3250 Pico2 that samples the TCD1304 output.
#
# Peter J. 2025-01-07
#          2025-01-08 Use base64 encoding of pixel data to send fewer bytes.
#
import argparse
import serial
import re
import serial.tools.list_ports as list_ports
import matplotlib.pyplot as plt

# -----------------------------------------------------------------------------
# Communication with the Pico2 happens through a standard serial port.
# Use pySerial to handle this connection.

def serial_ports():
    return [p.device for p in list_ports.comports()]

def openPort(port='/dev/ttyUSB0'):
    '''
    Returns a handle to the opened serial port, or None.
    '''
    sp = None
    try:
        sp = serial.Serial(port, 115200, rtscts=0, timeout=1.0)
    except serial.serialutil.SerialException:
        print(f'Did not find serial port: {port}')
        print(f'Serial ports that can be seen: {serial_ports()}')
        return None
    return sp

def send_command(sp, cmd_txt):
    '''
    Send the command text on the Pico2.
    '''
    sp.reset_input_buffer()
    cmd_bytes = f'{cmd_txt}\n'.encode('utf-8')
    # print("cmd_bytes=", cmd_bytes)
    sp.write(cmd_bytes)
    sp.flush()
    return

def get_short_text_response(sp):
    '''
    Returns the single line of text that comes back from a previously sent command.
    '''
    txt = sp.readline().strip().decode('utf-8')
    return txt

def get_long_text_response(sp, nlines):
    '''
    Returns many lines of text that for a longer response.
    '''
    lines = []
    while len(lines) < nlines:
        txt = sp.readline().strip().decode('utf-8')
        lines.append(txt)
    return lines

# -----------------------------------------------------------------------------
# Higher-level functions for interacting with the Pico2 that reads the TCD1304.

def get_pico_version_string(sp):
    '''
    Ask the Pico2 for its version string, so that we can see that it is alive.
    '''
    send_command(sp, 'v')
    txt = get_short_text_response(sp)
    if not txt.startswith('v'):
        raise RuntimeError(f'Unexpected response: {txt}')
    txt = re.sub('v', '', txt, count=1).strip()
    return txt

def sample_tcd1304_voltages(sp):
    '''
    The Pico records the TCD1304 voltages by taking 3800 ADC samples.

    Returns a short report giving the average, standard-deviation
    and time required to collect the samples.
    '''
    send_command(sp, 'b')
    txt = get_short_text_response(sp)
    if not txt.startswith('b'):
        raise RuntimeError(f'Unexpected response: {txt}')
    # print("txt=", txt)
    items = txt.split(' ')
    return {'v_average': float(items[1]),
            'v_stddev': float(items[2]),
            'time_us': float(items[3])}

def fetch_sampled_voltages(sp):
    '''
    Tell the Pico2 to actually report the sample values.
    The sample values (0-4095) are reported one per line by the Pico2.

    Returns the sample values as list of floating-point values.
    '''
    send_command(sp, 'r')
    txt_lines = get_long_text_response(sp, 3800)
    data = [float(v) for v in txt_lines]
    return data

#   0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
base64_alphabet = [
	'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P',
	'Q','R','S','T','U','V','W','X','Y','Z','a','b','c','d','e','f',
	'g','h','i','j','k','l','m','n','o','p','q','r','s','t','u','v',
	'w','x','y','z','0','1','2','3','4','5','6','7','8','9','+','/'
]
base64_values = {}
for i in range(len(base64_alphabet)):
    base64_values[base64_alphabet[i]] = i

def decode_base64_text_line(txt):
    data = []
    for k in range(20):
        hi = base64_values[txt[2*k]]
        lo = base64_values[txt[2*k+1]]
        data.append(float(hi*64+lo))
    return data

def fetch_sampled_voltages_quickly(sp):
    '''
    Tell the Pico2 to actually report the sample values.
    The 12-bit sample values (0-4095) are reported by the Pico2
    as pairs of base64 characters, 20 values per line.

    Returns the sample values as list of floating-point values.
    '''
    send_command(sp, 'q')
    txt_lines = get_long_text_response(sp, 3800/20)
    data = []
    for txt in txt_lines:
        data.extend(decode_base64_text_line(txt))
    return data

def set_SH_ICG_periods(sp, sh_us=200, icg_us=10000):
    '''
    sh_us sets the exposure period in microseconds
    icg_us is the read-out period in microseconds

    icg_us needs to be a neat multiple of sh_us
    so that the PWM signals stay in sync.

    icg_us needs to be greater then 7600 microseconds
    to give enough time for all of the pixel data
    to be clocked out.
    '''
    sh_us = int(sh_us)
    icg_us = int(icg_us)
    assert icg_us > 7600, "icg_us not large enough"
    assert icg_us % sh_us == 0, "icg_us not an exact multiple of sh_us"
    assert icg_us <= 32000 and sh_us <= 32000, "values are too large for 16-bit ints"
    send_command(sp, f'p {sh_us} {icg_us}')
    txt = get_short_text_response(sp)
    if not txt.startswith('p'):
        raise RuntimeError(f'Unexpected response: {txt}')
    print("txt=", txt)
    return

# -----------------------------------------------------------------------------

if __name__ == '__main__':
    # A basic procedure to drive the Pico2 and plot the pixel data.
    parser = argparse.ArgumentParser(description="TCD1304 linear image sensor test program")
    parser.add_argument('-p', '--port', dest='port', help='name for serial port')
    args = parser.parse_args()
    port_name = '/dev/ttyUSB0'
    if args.port: port_name = args.port
    sp = openPort(port_name)
    if sp:
        # We have successfully opened the serial port,
        # and we assume that the Pico2 is on the other end.
        sp.reset_input_buffer()
        sp.reset_output_buffer()
        print(get_pico_version_string(sp))
        set_SH_ICG_periods(sp, 300, 8400)
        print("Once sampling begins, use Ctrl-C (keyboard interrupt) to stop.")
        #
        # We explicitly start the graphics event loop so that we can later
        # update the displayed data.
        plt.ion()
        fig, ax = plt.subplots(1,1)
        ax.set_title(f'TCD1304 output as a 12-bit count')
        ax.set_xlim([0,3800])
        ax.set_ylim([1000,3500])
        ax.set_ylabel('ADC count')
        ax.set_xlabel('sample number')
        fig.canvas.draw()
        fig.canvas.flush_events()
        #
        # First collection of pixel data.
        stats = sample_tcd1304_voltages(sp)
        print(f"stats={stats}")
        data = fetch_sampled_voltages_quickly(sp)
        N = len(data)
        print(f"number of samples = {N}")
        line1, = ax.plot(data)
        fig.canvas.flush_events()
        try:
            # Continue getting fresh pixel data and updating the plot.
            while True:
                stats = sample_tcd1304_voltages(sp)
                print(f"stats={stats}")
                data = fetch_sampled_voltages_quickly(sp)
                line1.set_ydata(data)
                fig.canvas.flush_events()
        except KeyboardInterrupt:
            print("Stop asking for TCD1304 data.")
            # We try to read all of the bytes that may be in flight from the Pico2.
            # This is so that we don't have incoming junk at restart.
            junk_bytes = sp.read(3800*5)
            sp.flush()
    else:
        print("Did not find the serial port.")
    print("Done.")

