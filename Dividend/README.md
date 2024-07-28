Dividend looks at iqfeed fundamental data to look for dividend 
based equities.

Example output:

```
exchange,symbol,yield,rate,amount,vol,exdiv,payed,trade,option
AFCG,NASDAQ,11.98,2.24,0.56,218000,2022-Jun-29,2022-Jul-15,18.85,AFCG
ALTY,NASDAQ,8.21,0.9961,0.0672,14000,2022-Aug-03,2022-Aug-11,12.13,ALTY
BKCH,NASDAQ,13.55,0.9685,0.0345,193000,2022-Jun-29,2022-Jul-08,7.3,BKCH
BKEPP,NASDAQ,8.19,0.715,0.1787,28000,2022-Aug-04,2022-Aug-12,8.73,
CCAP,NASDAQ,9.27,1.64,0.05,49000,2022-Sep-01,2022-Sep-15,17.76,CCAP
COMT,NASDAQ,14.11,5.4941,5.4941,466000,2021-Dec-13,2021-Dec-17,38.93,COMT
....
```

The configuration file:

```
$ cat x64/debug/dividend.cfg
listed_market=NYSE
listed_market=NYSE_ARCA
listed_market=NYSE_AMERICAN
listed_market=NASDAQ
listed_market=NCM
listed_market=NGM
listed_market=NGSM
ignore_name=LYPHF
security_type=EQUITY
minimum_yield=8.0
minimum_volume=5000
max_in_transit=40
```

Something to consider for calculations:
* highest-yielding monthly dividend stocks with market capitalizations of at least $1 billion and payout ratios below 100%, meaning they are paying out less in dividends per share than they are bringing in in earnings per share (EPS)
* order by forward dividend yield, which is calculated by dividing the sum of a company’s projected dividend payouts over the next year by its current share price.

Source of ideas:
* [Best Monthly Dividend Stocks and ETFs](https://www.marketbeat.com/dividends/monthly-dividend-stocks/)
* [Stocks That Pay Monthly Dividends`](https://stockanalysis.com/list/monthly-dividend-stocks/)

Key Calculations:
* Market Capitalization = Outstanding Shares x Stock Price
* Earnings per Share = (Net Income - Preferred Dividends) ÷ Average Oustanding Common Shares
* Cash Flow per Share = (Operating Cash Flow – Preferred Dividends) ÷ Common Shares Outstanding
