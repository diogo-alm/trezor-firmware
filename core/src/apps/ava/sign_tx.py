from trezor.crypto import base58
from trezor.crypto.curve import secp256k1
from trezor.crypto.hashlib import sha256
from trezor.messages.AVASignedTx import AVASignedTx

from apps.ava import CURVE, SLIP44_ID
from apps.common import writers
from apps.common.seed import with_slip44_keychain


@with_slip44_keychain(SLIP44_ID, CURVE)
async def sign_tx(ctx, msg, keychain):

    node = keychain.derive(msg.address_n)

    num_outputs = len(msg.outputs)
    num_inputs = len(msg.inputs)

    w = bytearray()
    writers.write_uint32_be(w, 0)
    writers.write_uint32_be(w, msg.network)
    writers.write_bytes_unchecked(w, bytearray(msg.blockchain_id))

    # TODO: sort outputs
    writers.write_uint32_be(w, num_outputs)

    for output in msg.outputs:
        writers.write_bytes_unchecked(w, bytearray(output.asset))
        writers.write_uint32_be(w, output.fxid)
        writers.write_uint64_be(w, output.amount)
        writers.write_uint64_be(w, output.locktime)
        writers.write_uint32_be(w, output.threshold)

        # explain it will be shown and why
        addr = base58.decode(output.address[-2:])[
            :-4
        ]  # remove X- prepend and then the checksum
        writers.write_bytes_unchecked(w, bytearray(addr))

    # TODO: sort inputs
    writers.write_uint32_be(w, num_inputs)

    for inp in msg.inputs:
        writers.write_bytes_unchecked(w, bytearray(inp.txid))
        writers.write_uint32_be(w, inp.index)
        writers.write_bytes_unchecked(w, bytearray(inp.asset))
        writers.write_uint32_be(w, inp.fxid)
        writers.write_uint64_be(w, inp.amount)

    # sign
    digest = sha256(w).digest()
    signature = secp256k1.sign(node.private_key(), digest)

    writers.write_uint32_be(w, 0x09)  # SECP256K1 type credential
    writers.write_uint32_be(w, 1)  # number of signatures
    writers.write_bytes_unchecked(w, bytearray(signature))

    return AVASignedTx(tx=w)
