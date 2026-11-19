# Machine connections and API keys

## Manage machines from Overview

**Overview → Add machine** opens the connection form directly. A machine card opens its controls and also provides separate Edit and Remove actions. The individual machine page offers **Edit connection**, **Remove machine**, and **Test connection**. Connection management stays available when the machine is offline.

Workspace Settings contains equipment planning, display preferences, Brew Assistant configuration, and workspace exports. Machine registrations live on Overview.

The Hub reads the product model from authenticated, model-aware firmware and associates it with the verified device UID. New physical registrations do not default to Pro. Older firmware can use a manual model selection if it does not report an identity. A corrected current model does not rewrite archived brew snapshots, and a changed UID never silently replaces the registered appliance. The connection type stays fixed. The API-key field starts empty: leave it empty to keep the saved key. **Monitor this machine** enables or disables the Hub connection. The server rejects connection changes and removal while its last verified state reports an active operation.

**Brewer preferences** contains the machine's display preferences and a **Live readings** form. Receiver host, receiver port, and **Send live readings** are always visible for a physical machine. The live-data settings stay editable while the appliance is offline; display settings still require its connection.

The Hub stores the receiver settings and sends them to the machine when it connects or when the settings are saved. New connections start with this Hub's verified address. Existing custom host/port values and disabled sending settings are preserved. There is no automatic/manual selector or return-to-automatic action.

Saving while offline or busy retains the changes in the Hub and shows that application is pending. The Hub verifies the appliance identity and applies the settings after reconnecting and once the machine is idle. A successful save is distinct from a confirmed device update. These options do not change the Hub's saved machine address or API key. No destination form is required on the firmware website.

Removal requires confirmation and removes the Hub registration and saved credential. Archived brew records and their sensor samples remain available in Brew history. The Hub attempts to disable its own reporting connection on a reachable idle appliance; it does not disable a custom external destination or another Hub's connection. The action does not send a firmware erase, recipe deletion, or stop command to the appliance.

Old `#/settings/new-device` links redirect to the Overview add-machine dialog.

## One provisioned key per physical brewer

The standalone firmware authenticates requests using the key stored on that brewer at `/local/api.key`. Its provisioning tool generates a random 32-byte value represented by 64 lowercase hexadecimal characters. Paste that exact value into the Hub's **Machine API key** field.

Each physical brewer should have a separate randomly generated key. Multiple authorized clients connecting to the **same** brewer use that brewer's existing key. The credential is not a machine identifier; the authenticated status response provides the physical device UID, which the Hub pins to its registration.

The Hub can save a correctly formatted address and key while the machine is offline. Successful form submission therefore does not prove that the key authenticates: discovery and polling establish whether the appliance responds. An arbitrary new value typed into the Hub cannot change the value installed on the brewer.

### Shared-key advisory

The Hub marks registrations with **Shared API key** when their saved hexadecimal key values match another registered physical machine's. Disabled registrations are included; simulators are excluded. Editing, replacing a credential, removing a registration, and restarting the Hub recompute the advisory.

This warning does not reject valid existing credentials or rotate firmware keys. It also does not prove that two records refer to the same physical brewer, that either credential authenticates, or that a key is unique outside this Hub. Check whether the second record is an accidental duplicate. If there really are two brewers, provision a distinct key on each appliance and then update the corresponding Hub connection.

Only the advisory boolean is returned by the API as `key_reused`. Private comparison fingerprints remain in server memory; neither the fingerprints nor the credentials are included in device responses or workspace exports. Saved keys remain encrypted in the Hub's data directory. A credential that cannot be decrypted still produces a connection error.

For firmware-side provisioning and rotation, use the [standalone provisioning guide](../../firmware/pico-s-pro/modified/docs/standalone-provisioning.md). Changing a Hub registration is not a substitute for that operation.
