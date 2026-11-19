"""Verify slot receiver lifetimes and queued delivery across threads."""

import gc
import weakref
from concurrent.futures import ThreadPoolExecutor
from threading import Barrier

from PyQt6.QtCore import QCoreApplication, QObject, Qt, pyqtSignal


class Emitter(QObject):
    value = pyqtSignal(int)


def test_joined_emissions(app):
    sender = Emitter()
    received = []
    ready = Barrier(8)

    class Receiver(QObject):
        def accept(self, value):
            received.append(value)

    receiver = Receiver()
    sender.value.connect(receiver.accept, Qt.ConnectionType.QueuedConnection)

    def emit(worker):
        ready.wait(timeout=10)
        for value in range(worker * 32, (worker + 1) * 32):
            sender.value.emit(value)

    with ThreadPoolExecutor(max_workers=8) as executor:
        list(executor.map(emit, range(8)))
    app.processEvents()
    assert sorted(received) == list(range(256))
    sender.value.disconnect(receiver.accept)


def test_receiver_lifetime():
    sender = Emitter()
    received = []

    class Receiver:
        def accept(self, value):
            received.append(value)

    receiver = Receiver()
    reference = weakref.ref(receiver)
    sender.value.connect(receiver.accept)
    sender.value.emit(1)
    del receiver
    gc.collect()
    assert reference() is None
    sender.value.emit(2)
    assert received == [1]

    class SlottedReceiver:
        __slots__ = ()

        def accept(self, value):
            received.append(value)

    receiver = SlottedReceiver()
    sender.value.connect(receiver.accept)
    sender.value.emit(3)
    sender.value.disconnect(receiver.accept)
    sender.value.emit(4)
    assert received == [1, 3]


if __name__ == '__main__':
    application = QCoreApplication([])
    test_receiver_lifetime()
    test_joined_emissions(application)
