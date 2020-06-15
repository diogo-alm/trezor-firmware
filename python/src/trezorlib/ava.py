from . import messages
from .tools import expect


@expect(messages.AVAAddress, field="address")
def get_address(
    client, n,
):
    return client.call(messages.GetAVAAddress(address_n=n,))


@expect(messages.AVASignedTx, field="tx")
def sign_tx(
    client,
    network_id: int,
    blockchain_id: bytes,
    asset_id: bytes,
    address_n: list,
    transaction_id: bytes,
    utxo_index: int,
    utxo_amount: int,
    to_address: str,
    amount: int,
):

    outputs = [
        messages.AVAOutput(
            asset=asset_id,
            amount=amount,
            locktime=0,
            threshold=1,
            address=to_address,
        )
    ]

    inputs = [
        messages.AVAInput(
            address_n=address_n,
            txid=transaction_id,
            index=utxo_index,
            asset=asset_id,
            amount=utxo_amount,
        )
    ]

    return client.call(
        messages.AVASignTx(
            network=network_id,
            blockchain_id=blockchain_id,
            outputs=outputs,
            inputs=inputs,
        )
    )
