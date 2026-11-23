# Machine connections and API keys

## Manage machines from Brewery

**Brewery → Add machine** opens the registration form. For a machine running custom firmware, select **Pico standalone firmware**, enter a name and its **Machine URL**, and click **Add machine**. The Hub host must be able to reach that address. For the unmodified Zymatic, use its separate recipe-menu setup. [Owner guide](../../README.md) · [Stock Zymatic recipe sync](stock-zymatic.md)

On firmware that supports pairing, compare the code in the browser with the one on the brewer under **About → Pair with Hub** and approve it at the physical dial. On a pairing-capable Zymatic, use Ethernet and **About Zymatic**. When continuing from a completed Hub USB setup, the form uses the key saved with that job and checks the machine’s identity. For models without dial pairing or for an existing key file, choose **Use an existing key instead**.

Once registered, open **Manage machine** on its machine page for **Test connection**, **Edit connection**, or **Remove machine**. Connection management stays available offline. **Monitor this machine** enables or disables the Hub connection. The Hub reads the reported model when available and pins the first authenticated hardware UID. Older firmware can require manual model selection. A changed UID blocks the connection. Connection changes and removal are blocked while the last verified state reports an active operation.

**Brewer preferences** contains the machine's display preferences and a **Live readings** form. Receiver host, receiver port, and **Send live readings** are always visible for a physical machine. The live-data settings stay editable while the appliance is offline; display settings still require its connection.

The Hub stores the receiver settings and sends them to the machine when it connects or when the settings are saved. New connections start with this Hub's verified address. Existing custom host/port values and disabled sending settings are preserved. There is no automatic/manual selector or return-to-automatic action.

Saving while offline or busy retains the changes in the Hub and shows that application is pending. The Hub verifies the appliance identity and applies the settings after reconnecting and once the machine is idle. A successful save is distinct from a confirmed device update. These options do not change the Hub's saved machine address or API key. No destination form is required on the firmware website.

Removal requires confirmation and removes the Hub registration and saved credential. Archived brew records and their sensor samples remain available in **Brew journal**. The Hub attempts to disable its own reporting connection on a reachable idle appliance; it does not disable a custom external destination or another Hub's connection. The action does not send a firmware erase, recipe deletion, or stop command to the appliance.

Old `#/settings/new-device` links redirect to the Brewery add-machine dialog.

## One provisioned key per physical brewer

The Hub encrypts the device key it obtained during pairing or USB setup. The owner can use **Manage machine → Open machine website** on a connected machine that advertises pairing, or **Copy device API key** for manual sign-in. Ordinary machine responses and workspace exports omit credentials.

For manual enrollment, upload the private `api.key` file from setup or enter its 64 hexadecimal characters under **Machine API key**. The S/Pro provisioning tool generates a random 32-byte key and stores it at `/local/api.key` on that brewer. Multiple clients for one brewer use the same key. Use a distinct key for each physical machine.

A manual registration can be saved while the brewer is offline. Authentication and UID verification happen when the Hub reaches it. Use **Replace saved key** after provisioning a different key on the same brewer. Editing the registration updates the Hub’s copy.

### Shared-key advisory

The Hub marks registrations with **Shared API key** when their saved hexadecimal key values match another registered physical machine's. Disabled registrations are included; simulators are excluded. Editing, replacing a credential, removing a registration, and restarting the Hub recompute the advisory.

This warning does not reject valid existing credentials or rotate firmware keys. It also does not prove that two records refer to the same physical brewer, that either credential authenticates, or that a key is unique outside this Hub. Check whether the second record is an accidental duplicate. If there really are two brewers, provision a distinct key on each appliance and then update the corresponding Hub connection.

Only the advisory boolean is returned by the API as `key_reused`. Private comparison fingerprints remain in server memory; neither the fingerprints nor the credentials are included in device responses or workspace exports. Saved keys remain encrypted in the Hub's data directory. A credential that cannot be decrypted still produces a connection error.

For firmware-side provisioning and rotation, use the [standalone provisioning guide](../../firmware/pico-s-pro/modified/docs/standalone-provisioning.md). Changing a Hub registration is not a substitute for that operation.
