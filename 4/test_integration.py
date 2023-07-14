import fcntl
import os
import random
import shutil
import subprocess
from contextlib import nullcontext as does_not_raise
from dataclasses import dataclass
from tempfile import TemporaryDirectory
from typing import Callable, ContextManager, List, Optional

import pytest

MAJOR = 235
BUF_LEN = 128

random.seed(42)


def compile_and_run_c_prog(
    prog: str, dependencies: List[str]
) -> subprocess.CompletedProcess:
    with TemporaryDirectory() as d:
        for dep in dependencies:
            shutil.copy(dep, os.path.join(d, dep))
        with open(os.path.join(d, "prog.c"), "w") as f:
            f.write(prog)
        res = subprocess.run(["gcc", "-o", "prog", f.name])
        res.check_returncode()
        try:
            res = subprocess.run(
                ["./prog"], stdout=subprocess.PIPE, stderr=subprocess.PIPE
            )
            return res
        finally:
            os.remove("./prog")


def detect_msg_slot_channel_num() -> int:
    prog = '#include <stdio.h>\n#include "message_slot.h"\n\nint main(){printf("%d", MSG_SLOT_CHANNEL);}'
    res = compile_and_run_c_prog(prog, ["message_slot.h"])
    res.check_returncode()
    return int(res.stdout)


MSG_SLOT_CHANNEL = detect_msg_slot_channel_num()


@dataclass
class Operation:
    exp_exception: ContextManager

    def execute(self):
        pass

    def cleanup(self):
        pass


@dataclass
class CreateSlot(Operation):
    name: str
    minor: int

    def execute(self):
        self.filename = f"/dev/{self.name}"
        os.system(f"sudo mknod {self.filename} c {MAJOR} {self.minor}")
        os.system(f"sudo chmod a+rw {self.filename}")

    def cleanup(self):
        if os.path.exists(self.filename):
            os.system(f"sudo rm {self.filename}")


@dataclass
class DeleteSlot(Operation):
    name: str

    def execute(self):
        filename = f"/dev/{self.name}"
        os.system(f"sudo rm {filename}")


@dataclass
class Read(Operation):
    filename: str
    channel: Optional[int]
    n: int
    expected: bytes
    preprocessor: Callable[[bytes], bytes] = lambda x: x

    def execute(self):
        f = os.open(self.filename, os.O_RDONLY)
        try:
            if self.channel is not None:
                fcntl.ioctl(f, MSG_SLOT_CHANNEL, self.channel)
            msg = os.read(f, self.n)
        finally:
            os.close(f)
        assert self.preprocessor(msg) == self.expected


@dataclass
class Send(Operation):
    filename: str
    channel: Optional[int]
    msg: bytes

    def execute(self):
        f = os.open(self.filename, os.O_WRONLY)
        try:
            if self.channel is not None:
                fcntl.ioctl(f, MSG_SLOT_CHANNEL, self.channel)
            os.write(f, self.msg)
        finally:
            os.close(f)


@dataclass
class CompileSender(Operation):
    def execute(self):
        if os.system("gcc -O3 -Wall -std=c11 message_sender.c -o message_sender") > 0:
            raise Exception("message_sender compilation failed")

    def cleanup(self):
        if os.path.exists("./message_sender"):
            os.remove("./message_sender")


@dataclass
class CompileReader(Operation):
    def execute(self):
        if os.system("gcc -O3 -Wall -std=c11 message_reader.c -o message_reader") > 0:
            raise Exception("message_reader compilation failed")

    def cleanup(self):
        if os.path.exists("./message_reader"):
            os.remove("./message_reader")


@dataclass
class ReaderRead(Read):
    def execute(self):
        res = subprocess.run(
            ["./message_reader", self.filename, f"{self.channel}"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        res.check_returncode()
        assert self.preprocessor(res.stdout) == self.expected


@dataclass
class SenderSend(Send):
    def execute(self):
        res = subprocess.run(
            ["./message_sender", self.filename, f"{self.channel}", self.msg],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        res.check_returncode()


@dataclass
class CustomCProg(Operation):
    prog: str
    dependencies: List[str]
    exp_exit_code: int
    exp_stdout: bytes
    exp_stderr: bytes

    def execute(self):
        res = compile_and_run_c_prog(self.prog, self.dependencies)
        assert res.returncode == self.exp_exit_code
        assert res.stderr == self.exp_stderr
        if self.exp_exit_code == 0:
            assert res.stdout == self.exp_stdout


def order_retaining_merge(*lsts: list):
    indices = [0 for _ in range(len(lsts))]
    merged = []
    for _ in range(sum(len(lst) for lst in lsts)):
        i = random.choice([i for i in range(len(indices)) if indices[i] < len(lsts[i])])
        merged.append(lsts[i][indices[i]])
        indices[i] += 1
    return merged


def generate_send_read_ops(n: int) -> List[Operation]:
    ops = []
    for i in range(n):
        seq: List[Operation] = [
            CreateSlot(
                name=f"message_slot{i}", minor=i, exp_exception=does_not_raise()
            ),
        ]
        seq += order_retaining_merge(
            *[
                [
                    Send(
                        filename=f"/dev/message_slot{i}",
                        channel=j,
                        msg=f"slot {i} channel {j}".encode(),
                        exp_exception=does_not_raise(),
                    ),
                    Read(
                        filename=f"/dev/message_slot{i}",
                        channel=j,
                        n=BUF_LEN,
                        expected=f"slot {i} channel {j}".encode(),
                        preprocessor=lambda x: x.decode().rstrip("\x00").encode(),
                        exp_exception=does_not_raise(),
                    ),
                ]
                for j in range(1, n)
            ]
        )
        seq.append(DeleteSlot(name=f"message_slot{i}", exp_exception=does_not_raise()))
        ops.append(seq)
    return order_retaining_merge(*ops)


test_cases = [
    pytest.param(
        [
            CreateSlot(name="message_slot42", minor=4, exp_exception=does_not_raise()),
            Send(
                filename="/dev/message_slot42",
                channel=13,
                msg=b"hello world",
                exp_exception=does_not_raise(),
            ),
            Read(
                filename="/dev/message_slot42",
                channel=13,
                n=BUF_LEN,
                expected=b"hello world",
                preprocessor=lambda x: x.decode().rstrip("\x00").encode(),
                exp_exception=does_not_raise(),
            ),
            DeleteSlot(name="message_slot42", exp_exception=does_not_raise()),
        ],
        id="sanity",
    ),
    pytest.param(
        [
            CreateSlot(name="message_slot42", minor=4, exp_exception=does_not_raise()),
            CompileSender(exp_exception=does_not_raise()),
            CompileReader(exp_exception=does_not_raise()),
            SenderSend(
                filename="/dev/message_slot42",
                channel=13,
                msg=b"hello world",
                exp_exception=does_not_raise(),
            ),
            ReaderRead(
                filename="/dev/message_slot42",
                channel=13,
                n=BUF_LEN,
                expected=b"hello world",
                preprocessor=lambda x: x.decode().rstrip("\x00").encode(),
                exp_exception=does_not_raise(),
            ),
            DeleteSlot(name="message_slot42", exp_exception=does_not_raise()),
        ],
        id="use message_sender and message_reader",
    ),
    pytest.param(
        [
            CreateSlot(name="message_slot42", minor=4, exp_exception=does_not_raise()),
            Send(
                filename="/dev/message_slot42",
                channel=13,
                msg=b"hello world",
                exp_exception=does_not_raise(),
            ),
            Send(
                filename="/dev/message_slot42",
                channel=99999,
                msg=b"goodbye world",
                exp_exception=does_not_raise(),
            ),
            Read(
                filename="/dev/message_slot42",
                channel=99999,
                n=BUF_LEN,
                expected=b"goodbye world",
                preprocessor=lambda x: x.decode().rstrip("\x00").encode(),
                exp_exception=does_not_raise(),
            ),
            Read(
                filename="/dev/message_slot42",
                channel=13,
                n=BUF_LEN,
                expected=b"hello world",
                preprocessor=lambda x: x.decode().rstrip("\x00").encode(),
                exp_exception=does_not_raise(),
            ),
            DeleteSlot(name="message_slot42", exp_exception=does_not_raise()),
        ],
        id="writing to multiple channels and then reading",
    ),
    pytest.param(
        [
            CreateSlot(name="message_slot42", minor=4, exp_exception=does_not_raise()),
            Send(
                filename="/dev/message_slot42",
                channel=13,
                msg=b"hello world",
                exp_exception=does_not_raise(),
            ),
            Read(
                filename="/dev/message_slot42",
                channel=13,
                n=BUF_LEN,
                expected=b"hello world",
                preprocessor=lambda x: x.decode().rstrip("\x00").encode(),
                exp_exception=does_not_raise(),
            ),
            Send(
                filename="/dev/message_slot42",
                channel=13,
                msg=b"goodbye",
                exp_exception=does_not_raise(),
            ),
            Read(
                filename="/dev/message_slot42",
                channel=13,
                n=BUF_LEN,
                expected=b"goodbye",
                preprocessor=lambda x: x.decode().rstrip("\x00").encode(),
                exp_exception=does_not_raise(),
            ),
            DeleteSlot(name="message_slot42", exp_exception=does_not_raise()),
        ],
        id="writing multiple times leaves only latest message",
    ),
    pytest.param(
        [
            CreateSlot(name="message_slot42", minor=4, exp_exception=does_not_raise()),
            Send(
                filename="/dev/message_slot42",
                channel=13,
                msg=b"hello world",
                exp_exception=does_not_raise(),
            ),
            CreateSlot(name="message_slot45", minor=7, exp_exception=does_not_raise()),
            Send(
                filename="/dev/message_slot45",
                channel=99999,
                msg=b"goodbye world",
                exp_exception=does_not_raise(),
            ),
            Read(
                filename="/dev/message_slot45",
                channel=99999,
                n=BUF_LEN,
                expected=b"goodbye world",
                preprocessor=lambda x: x.decode().rstrip("\x00").encode(),
                exp_exception=does_not_raise(),
            ),
            Read(
                filename="/dev/message_slot42",
                channel=13,
                n=BUF_LEN,
                expected=b"hello world",
                preprocessor=lambda x: x.decode().rstrip("\x00").encode(),
                exp_exception=does_not_raise(),
            ),
            DeleteSlot(name="message_slot42", exp_exception=does_not_raise()),
            DeleteSlot(name="message_slot45", exp_exception=does_not_raise()),
        ],
        id="writing to multiple channels in multiple slots and then reading",
    ),
    pytest.param(
        [
            CreateSlot(name="message_slot42", minor=5, exp_exception=does_not_raise()),
            Send(
                filename="/dev/message_slot42",
                channel=13,
                msg=b"z" * 500,
                exp_exception=pytest.raises(OSError, match="Message too long"),
            ),
            DeleteSlot(name="message_slot42", exp_exception=does_not_raise()),
        ],
        id="message too long write error",
    ),
    pytest.param(
        [
            CreateSlot(name="message_slot42", minor=5, exp_exception=does_not_raise()),
            Send(
                filename="/dev/message_slot42",
                channel=13,
                msg=b"lorem ipsum",
                exp_exception=does_not_raise(),
            ),
            Read(
                filename="/dev/message_slot42",
                channel=13,
                n=2,
                expected=b"lo",
                exp_exception=pytest.raises(OSError, match="No space left on device"),
            ),
            DeleteSlot(name="message_slot42", exp_exception=does_not_raise()),
        ],
        id="read buffer too small error",
    ),
    pytest.param(
        [
            CreateSlot(name="message_slot43", minor=6, exp_exception=does_not_raise()),
            Send(
                filename="/dev/message_slot43",
                channel=97,
                msg=b"lorem ipsum",
                exp_exception=does_not_raise(),
            ),
            Read(
                filename="/dev/message_slot43",
                channel=None,
                n=BUF_LEN,
                expected=b"",
                exp_exception=pytest.raises(OSError, match="Invalid argument"),
            ),
            DeleteSlot(name="message_slot43", exp_exception=does_not_raise()),
        ],
        id="reading before setting channel error",
    ),
    pytest.param(
        [
            CreateSlot(name="message_slot43", minor=6, exp_exception=does_not_raise()),
            Send(
                filename="/dev/message_slot43",
                channel=None,
                msg=b"lorem ipsum",
                exp_exception=pytest.raises(OSError, match="Invalid argument"),
            ),
            DeleteSlot(name="message_slot43", exp_exception=does_not_raise()),
        ],
        id="writing before setting channel error",
    ),
    pytest.param(
        [
            CreateSlot(name="message_slot42", minor=4, exp_exception=does_not_raise()),
            Read(
                filename="/dev/message_slot42",
                channel=22,
                n=BUF_LEN,
                expected=b"",
                exp_exception=pytest.raises(
                    OSError, match="Resource temporarily unavailable"
                ),
            ),
            DeleteSlot(name="message_slot42", exp_exception=does_not_raise()),
        ],
        id="reading from empty channel error",
    ),
    pytest.param(
        [
            CreateSlot(name="message_slot42", minor=4, exp_exception=does_not_raise()),
            CustomCProg(
                prog="""
                #include <stdio.h>
                #include <stdlib.h>
                #include <fcntl.h>
                #include <unistd.h>
                #include "message_slot.h"
                int main() {
                    int fd = open("/dev/message_slot42", O_WRONLY);
                    if (fd < 0) {
                        perror("open");
                        return 1;
                    }
                    if (ioctl(fd, MSG_SLOT_CHANNEL, 22) < 0) {
                        perror("ioctl");
                        return 1;
                    }
                    if (write(fd, (void *)128, 12) < 0) {
                        fprintf(stderr, "write");
                        return 2;
                    }
                    return 0;
                }
                """,
                dependencies=["message_slot.h"],
                exp_exit_code=2,
                exp_stdout=b"",
                exp_stderr=b"write",
                exp_exception=does_not_raise(),
            ),
        ],
        id="invalid buffer pointer error",
    ),
    pytest.param(
        generate_send_read_ops(128),
        id="stress testing - multi slot multi channel",
    ),
]


def install_device():
    os.system("make")
    os.system("sudo insmod message_slot.ko")
    os.system("make clean")


def uninstall_device():
    os.system("sudo rmmod message_slot")


@pytest.fixture(autouse=True)
def device_driver():
    install_device()
    yield
    uninstall_device()


@pytest.mark.parametrize("operations", test_cases)
def test_message_slot(operations: List[Operation]):
    cleanups: List[Callable] = []
    try:
        for op in operations:
            with op.exp_exception:
                op.execute()
            cleanups.append(op.cleanup)
    finally:
        for cleanup in reversed(cleanups):
            cleanup()
