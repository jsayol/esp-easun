const networkList = document.querySelector("#networks");

networkList.disabled = true;
networkList.selectedOptions[0].innerText = "Scanning WiFi...";

networkList.addEventListener("change", () => {
    setTimeout(() => {
        document.querySelector('#password').focus();
    }, 100);
})

fetch("/scan")
    .then(scan => scan.json()).then(scan => {
        scan.sort((a, b) => a.rssi > b.rssi ? -1 : 1);
        scan.forEach(network => {
            const opt = document.createElement("option");
            opt.value = network.ssid;
            opt.innerText = network.ssid;
            networkList.appendChild(opt);
        });
        networkList.disabled = false;
        networkList.selectedOptions[0].innerText = "Discovered WiFi networks";
    })
    .catch(err => {
        alert("Error while fetching the list of available WiFi networks");
    });