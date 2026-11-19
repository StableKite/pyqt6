from PyQt6.QtCore import QIODevice


class MemoryDevice(QIODevice):
    def __init__(self):
        super().__init__()
        self.input_data = bytearray(b'abc\nnext')
        self.written = None
        self.read_line_invoked = False

    def isSequential(self):
        return True

    def readData(self, limit):
        data, self.input_data = self.input_data[:limit], self.input_data[limit:]
        return memoryview(data)

    def readLineData(self, limit):
        self.read_line_invoked = True
        return self.readData(min(limit, self.input_data.index(b'\n') + 1))

    def writeData(self, size):
        self.written = bytearray(size)
        return memoryview(self.written)


device = MemoryDevice()
assert device.open(QIODevice.OpenModeFlag.ReadWrite | QIODevice.OpenModeFlag.Unbuffered)
assert device.readLine(32) == b'abc\n'
assert device.read_line_invoked
assert device.read(32) == b'next'
assert device.write(memoryview(b'buffer payload')) == len(b'buffer payload')
assert device.written == b'buffer payload'
device.close()
