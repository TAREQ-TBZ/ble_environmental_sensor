import serial
import re

from time import time

from pynrfjprog import LowLevel


class BOARD:
    def __init__(self, port, baud, fw_image):
        self.port = port
        self.baud = baud
        self.fw_image = fw_image

        self.program(fw_image)

        self.serial_device = serial.Serial(port, self.baud, timeout=1, write_timeout=1)

    def program(self, fw_image):
        with LowLevel.API() as api:
            api.connect_to_emu_without_snr()
            api.recover()
            api.erase_all()
            api.program_file(self.fw_image)
            api.sys_reset()
            api.go()
            api.close()

    def hard_reset(self):
        with LowLevel.API() as api:
            api.connect_to_emu_without_snr()
            api.hard_reset()
            api.go()
            api.close()

    def wait_for_regex_in_line(self, regex, timeout_s=500, log=True):
        start_time = time()
        while True:
            self.serial_device.timeout = timeout_s
            line = (
                self.serial_device.read_until()
                .decode("utf-8", errors="replace")
                .replace("\r\n", "")
            )
            if line != "" and log:
                print(line)
            if time() - start_time > timeout_s:
                raise RuntimeError("Timeout")
            regex_search = re.search(regex, line)
            if regex_search:
                return regex_search

    def close(self):
        if self.serial_device:
            self.serial_device.close()
