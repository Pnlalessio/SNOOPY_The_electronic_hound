#!/usr/bin/env python
"""
Collects sensor data through a serial connection, organizes it in CSV format, 
and stores it in separate files. The data should follow a specific format: the first line acts as a header, 
each sample is on a new line, and samples are separated by an empty line. 

---------------------------------------------------------------------------------------------------------------------------------

You can invoke this Python script with specific command-line arguments. 
This script is named collect_csv_from_serial_port.py. You can run the script from the command line like this:

'python collect_csv_from_serial_port.py -p COM4 -b 115200 -d /path/to/output -l my_data'

Let's break down what each part of this command does:

python collect_csv_from_serial_port.py: This is the basic command to run the Python script.

-p COM4: This is how you specify the serial port argument. -p corresponds to the short argument name, and COM4 is the value you're providing for the port argument.

-b 115200: This is how you specify the baud rate argument. -b corresponds to the short argument name, and 115200 is the value you're providing for the baud argument.

-d /path/to/output: This is how you specify the output directory argument. -d corresponds to the short argument name, and /path/to/output is the value you're providing for the directory argument.

-l my_data: This is how you specify the label argument. -l corresponds to the short argument name, and my_data is the value you're providing for the label argument (e.g. coffee).

"""

import argparse          # Library for parsing command-line arguments
import os                # Library for handling file and directory operations
import time              # Library for time-related functions
import serial            # Library for serial communication
import serial.tools.list_ports  # Library for listing available serial ports

DEFAULT_BAUD = 115200       # Default baud rate for the serial communication
DEFAULT_LABEL = "_unknown"  # Default label for output files

def write_csv_data_to_file(data, directory, label):
    """
    Write CSV data to a file with a unique filename.
    """
    exists = True
    while exists:
        uid = str(round(time.time() * 1000))  # Generate a unique ID using current time
        filename = label + "." + uid + ".csv"  # Construct a unique filename
        out_path = os.path.join(directory, filename)  # Combine directory and filename
        
        if not os.path.exists(out_path):
            exists = False
            try:
                with open(out_path, 'w') as file:
                    file.write(data)  # Write the CSV data to the file
                print("Data written to:", out_path)
            except IOError as e:
                print("ERROR", e)
                return

def main():
    parser = argparse.ArgumentParser(description="Serial Data Collection CSV")
    parser.add_argument('-p', '--port', dest='port', type=str, required=True, help="Serial port to connect to")
    parser.add_argument('-b', '--baud', dest='baud', type=int, default=DEFAULT_BAUD, help="Baud rate (default = " + str(DEFAULT_BAUD) + ")")
    parser.add_argument('-d', '--directory', dest='directory', type=str, default=".", help="Output directory for files (default = .)")
    parser.add_argument('-l', '--label', dest='label', type=str, default=DEFAULT_LABEL, help="Label for files (default = " + DEFAULT_LABEL + ")")

    # Print information about available serial ports
    print("\nAvailable serial ports:")
    available_ports = serial.tools.list_ports.comports()
    for port, desc, hwid in sorted(available_ports):
        print("  {} : {} [{}]".format(port, desc, hwid))
    
    # Parse command-line arguments
    args = parser.parse_args()
    serial_port = args.port
    baud_rate = args.baud
    output_directory = args.directory
    label = args.label

    # Create a serial connection object
    ser = serial.Serial()
    ser.port = serial_port
    ser.baudrate = baud_rate

    try:
        ser.open()  # Open the serial port for communication
    except Exception as e:
        print("ERROR:", e)
        exit()
    print("\nConnected to {} at a baud rate of {}".format(serial_port, baud_rate))
    print("Press 'ctrl+c' to exit")

    rx_buffer = b''  # Initialize an empty buffer for receiving data

    try:
        skip_first_samples = 0
        while skip_first_samples <= 151:
            if ser.in_waiting > 0:  # Check if there is data available in the serial buffer
                while ser.in_waiting:
                    rx_buffer += ser.read()  # Read and append data from the serial buffer
                    if rx_buffer[-4:] == b'\r\n\r\n':  # Check for the end of a sample
                        buffer_str = rx_buffer.decode('utf-8').strip()  # Convert bytes to a string
                        buffer_str = buffer_str.replace('\r', '')  # Remove carriage return characters
                        if skip_first_samples > 0:
                            write_csv_data_to_file(buffer_str, output_directory, label)  # Save data to CSV file
                        rx_buffer = b''  # Reset the buffer
                        skip_first_samples += 1
    except KeyboardInterrupt:
        pass

    print("Closing the serial port")
    ser.close()  # Close the serial port when done

if __name__ == "__main__":
    main()

