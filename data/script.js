const networkList = document.querySelector("#networks");

networkList.disabled = true;
networkList.selectedOptions[0].innerText = "Scanning WiFi...";

networkList.addEventListener("change", () => {
    setTimeout(() => {
        document.querySelector('#password').focus();
    }, 100);
})

function rssiToPcent(rssi) {
    return Math.min((rssi + 100) * 2, 100);
}

function fetchNetworks(retry = false) {
    return fetch("/scan")
        .then(scan => scan.json()).then(scan => {
            if ((scan.length === 0) && retry) {
                return fetchNetworks();
            }

            scan.sort((a, b) => a.rssi > b.rssi ? -1 : 1);

            scan.forEach(network => {
                const opt = document.createElement("option");
                opt.value = network.ssid;
                opt.innerText = `[${String(rssiToPcent(network.rssi)).padStart(3, " ")}%] ${network.ssid}`;
                networkList.appendChild(opt);
            });

            if (scan.length === 0) {
                networkList.selectedOptions[0].innerText = "No networks discovered";
            } else {
                networkList.disabled = false;
                networkList.selectedOptions[0].innerText = "Discovered WiFi networks:";
            }
        })
        .catch(err => {
            alert("Error while fetching the list of available WiFi networks");
        });
}

fetchNetworks(true);