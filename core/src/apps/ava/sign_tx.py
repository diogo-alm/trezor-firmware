from trezor.crypto import base58
from trezor.crypto.curve import secp256k1_zkp, secp256k1
from trezor.crypto.hashlib import sha256
from trezor.messages.AVASignedTx import AVASignedTx
from binascii import hexlify
from apps.ava import CURVE, SLIP44_ID
from apps.common import writers
from apps.common.seed import with_slip44_keychain

@with_slip44_keychain(SLIP44_ID, CURVE)
async def sign_tx(ctx, msg, keychain):

    num_outputs = len(msg.outputs)
    num_inputs = len(msg.inputs)

    w = bytearray()

    writers.write_uint32_be(w, 0)
    writers.write_uint32_be(w, msg.network)
    writers.write_bytes_fixed(w, bytearray(msg.blockchain_id), 32)

    # TODO: sort outputs
    writers.write_uint32_be(w, num_outputs)

    for output in msg.outputs:
        writers.write_bytes_fixed(w, bytearray(output.asset), 32)
        writers.write_uint32_be(w, 0x07) # secp256k1 output
        writers.write_uint64_be(w, output.amount)
        writers.write_uint64_be(w, output.locktime)
        writers.write_uint32_be(w, output.threshold)

        addr = base58.decode(output.address[2:])[
            :-4
        ]  # remove X- prepend and checksum
        writers.write_uint32_be(w, 0x01)
        writers.write_bytes_fixed(w, bytearray(addr), 20)

    # TODO: sort inputs
    writers.write_uint32_be(w, num_inputs)

    for inp in msg.inputs:
        writers.write_bytes_fixed(w, bytearray(inp.txid), 32)
        writers.write_uint32_be(w, inp.index)
        writers.write_bytes_fixed(w, bytearray(inp.asset), 32)


        writers.write_uint32_be(w, 0x05) # secp256k1 input
        writers.write_uint64_be(w, inp.amount)

        # TODO
        writers.write_uint32_be(w, 0x01)
        writers.write_uint32_be(w, 0x00)

    # sign
    digest = sha256(w).digest()
    ctx = secp256k1_zkp.Context()

    # TODO: multiple inputs can have different derivation paths
    node = keychain.derive(msg.inputs[0].address_n)
    signature = secp256k1.sign(node.private_key(), digest, False)
    signature = signature[1:] + bytearray([signature[0] - 27])

    writers.write_uint32_be(w, 0x01)  # Number of credentials

    # TODO: match sig with addressindices in inputs
    writers.write_uint32_be(w, 0x09) # sepc256k1 credential
    writers.write_uint32_be(w, 0x01)
    writers.write_bytes_fixed(w, bytearray(signature), 65)

    return AVASignedTx(tx=w)
