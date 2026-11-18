"""Exercise the process-wide Qt log handler from concurrent callers."""

from concurrent.futures import ThreadPoolExecutor
from queue import SimpleQueue
from threading import Barrier

from PyQt6.QtCore import qInstallMessageHandler, qWarning


def test_concurrent_replacement():
    messages = SimpleQueue()
    ready = Barrier(8)

    def first(_kind, _context, message):
        messages.put(message)

    def second(_kind, _context, message):
        messages.put(message)

    def send(worker):
        ready.wait(timeout=10)
        for index in range(250):
            qWarning(f'{worker}:{index}')

    def replace():
        ready.wait(timeout=10)
        for _ in range(125):
            assert qInstallMessageHandler(first) in (first, second)
            assert qInstallMessageHandler(second) in (first, second)

    saved = qInstallMessageHandler(first)
    try:
        with ThreadPoolExecutor(max_workers=8) as pool:
            workers = [pool.submit(send, worker) for worker in range(4)]
            workers.extend(pool.submit(replace) for _ in range(4))
            for worker in workers:
                worker.result(timeout=30)

        seen = {messages.get_nowait() for _ in range(1000)}
        assert seen == {f'{worker}:{index}' for worker in range(4) for index in range(250)}
        assert messages.empty()

        def replace_from_callback(_kind, _context, message):
            messages.put(message)
            qInstallMessageHandler(first)

        qInstallMessageHandler(replace_from_callback)
        qWarning('reentrant')
        qWarning('replacement')
        assert messages.get_nowait() == 'reentrant'
        assert messages.get_nowait() == 'replacement'
        assert messages.empty()
    finally:
        qInstallMessageHandler(saved)


if __name__ == '__main__':
    test_concurrent_replacement()
