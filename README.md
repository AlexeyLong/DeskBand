# Cryptocurrency Price Ticker v1.0.2
Windows taskbar DeskBand — displays live crypto prices.

## Build
1. Open DeskBand.sln in Visual Studio 2019/2022
2. Select Release | x64
3. Ctrl+Shift+B
4. Output: x64\Release\DeskBand.dll

## Install (run as Administrator)
```
regsvr32 "C:\path\to\DeskBand.dll"
```
Then right-click taskbar → Toolbars → Cryptocurrency Price Ticker

## Uninstall
```
regsvr32 /u "C:\path\to\DeskBand.dll"
```

## Menu
Right-click the ticker to access settings:

| Menu | Options |
|---|---|
| Update Style | No highlighting / Highlight text only / Highlight background |
| Update Frequency | 30 seconds / 1 minute / 5 minutes |
| Cryptocurrencies | BTCUSD / ETHUSD / BNBUSD / LTCUSD |
| Market Data Provider | CoinGecko / Binance Futures / OKX |

## Providers
| Provider | Endpoint | Type |
|---|---|---|
| CoinGecko | api.coingecko.com | Spot USD aggregated |
| Binance Futures | fapi.binance.com | Perpetual futures USDT |
| OKX | www.okx.com | Spot USDT |

## Notes
- Switching provider takes effect immediately, no restart required
- If connection fails, ticker shows "Refreshing..." until next successful fetch
- All settings saved to HKCU\Software\CryptoPriceTicker
- After rebuilding: regsvr32 /u old DLL first, then regsvr32 new DLL
