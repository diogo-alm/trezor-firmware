from . import messages
from .tools import expect


@expect(messages.AVAAddress, field="address")
def get_address(
    client, n,
):
    return client.call(messages.GetAVAAddress(address_n=n,))
