/************************************************************************
 * Copyright(c) 2024, One Unified. All rights reserved.                 *
 * email: info@oneunified.net                                           *
 *                                                                      *
 * This file is provided as is WITHOUT ANY WARRANTY                     *
 *  without even the implied warranty of                                *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.                *
 *                                                                      *
 * This software may not be used nor distributed without proper license *
 * agreement.                                                           *
 *                                                                      *
 * See the file LICENSE.txt for redistribution information.             *
 ************************************************************************/

/*
 * File:    Strategy.cpp
 * Author:  raymond@burkholder.net
 * Project: CurrencyTrader
 * Created: March 14, 2024 21:32:07
 */

// see ATR trading notes at bottom
// TODO: load daily bars to calculate TR over last number of days
// TODO: change some of the screeners to use TR rather than bar ranges

// TODO: how many swings are on the proper side of ema?

// https://www.forex.com/en-us/forex-trading/how-to-trade-forex/ talks about the over-night roll-over fees

// "Pip" is an acronym for Percentage in Point or Price Interest Point.

#include <boost/log/trivial.hpp>

#include "Strategy.hpp"

Strategy::Strategy()
: DailyTradeTimeFrame<Strategy>()
, m_eBaseCurrency( EBase::Unknown )
, m_quantityToOrder {}
, m_tick {}
, m_base_currency_pip {}
, m_bfQuotes01Sec( 1 )
, m_bfTrading( 60 )
, m_ceSwingHi( ou::ChartEntryShape::EShape::Long,  ou::Colour::Purple )
, m_ceSwingLo( ou::ChartEntryShape::EShape::Short, ou::Colour::HotPink )
, m_nLo {}, m_nNet {}, m_nHi {}
, m_fResetSoftware( nullptr )
{
  Init();
}

void Strategy::Init() {

  m_ceQuoteAsk.SetName( "Ask" );
  m_ceTrade.SetName(    "Tick" );
  m_ceQuoteBid.SetName( "Bid" );

  m_ceBarsTrade.SetName( "Trades" );

  m_ceVolume.SetName(   "Volume" );

  m_ceQuoteAsk.SetColour( ou::Colour::Red );
  m_ceTrade.SetColour( ou::Colour::DarkGreen );
  m_ceQuoteBid.SetColour( ou::Colour::Blue );

  m_cdv.Add( EChartSlot::Price, &m_ceSwingLo );
  m_cdv.Add( EChartSlot::Price, &m_ceSwingHi );

  m_cdv.Add( EChartSlot::Price, &m_ceQuoteAsk );
  m_cdv.Add( EChartSlot::Price, &m_ceTrade );
  m_cdv.Add( EChartSlot::Price, &m_ceQuoteBid );

  m_cdv.Add( EChartSlot::Price, &m_ceBarsTrade );

  m_cdv.Add( EChartSlot::Volume, &m_ceVolume );

  m_plUp.Init( m_cdv, EChartSlot::PL_Up );
  m_plTtl.Init( m_cdv, EChartSlot::PL_Ttl );
  m_plDn.Init( m_cdv, EChartSlot::PL_Dn );

  m_ceTradingRangeRising.SetColour( ou::Colour::Green );
  m_ceTradingRangeRising.SetName( "TR Rise" );
  m_ceTradingRangeFalling.SetColour( ou::Colour::Red );
  m_ceTradingRangeFalling.SetName( "TR Fall" );

  m_cdv.Add( EChartSlot::TR_EMA, &m_ceTradingRangeRising );
  m_cdv.Add( EChartSlot::TR_EMA, &m_ceTradingRangeFalling );

  // supplied by 1 second mid-quote
  m_pEmaCurrency = std::make_unique<EMA>( 3 * 60, m_cdv, EChartSlot::Price );
  m_pEmaCurrency->Set( ou::Colour::Purple, "Price EMA" );

  // supplied by 1 minute trade bar
  m_pATRFast = std::make_unique<EMA>( 3, m_cdv, EChartSlot::ATR );
  m_pATRFast->Set( ou::Colour::Blue, "ATR Fast" );

  // supplied by 1 minute trade bar
  m_pATRSlow = std::make_unique<EMA>( 14, m_cdv, EChartSlot::ATR );
  m_pATRSlow->Set( ou::Colour::Crimson, "ATR Slow" );

  m_bfQuotes01Sec.SetOnBarComplete( MakeDelegate( this, &Strategy::HandleBarQuotes01Sec ) );
  m_bfTrading.SetOnBarComplete( MakeDelegate( this, &Strategy::HandleMinuteBar ) );
}

Strategy::~Strategy() {

  if ( m_pWatch ) {
    m_pWatch->OnQuote.Remove( MakeDelegate( this, &Strategy::HandleQuote ) );
    m_pWatch->OnTrade.Remove( MakeDelegate( this, &Strategy::HandleTrade ) );
    //m_pWatch->RecordSeries( true );
  }

  m_bfQuotes01Sec.SetOnBarComplete( nullptr );
  m_bfTrading.SetOnBarComplete( nullptr );

  m_pATRFast.reset();
  m_pATRSlow.reset();
  m_pEmaCurrency.reset();

  m_cdv.Clear();
}

void Strategy::SetTransaction( ou::tf::Order::quantity_t quantity, fTransferFunds_t&& f ) {

  m_quantityToOrder = quantity;
  m_to_up.Set( quantity, f ); // make a copy of f
  m_to_dn.Set( quantity, f ); // make a copy of f

}

void Strategy::SetWatch( EBase eBaseCurrency, pWatch_t pWatch, pPortfolio_t pPortfolio, fConstructPosition_t&& f ) {

  assert( pWatch );
  assert( pPortfolio );
  assert( EBase::Unknown != eBaseCurrency );

  m_eBaseCurrency = eBaseCurrency;

  m_pWatch = pWatch;
  m_pPortfolio = pPortfolio; // TODO: update portfolio metrics to chart

  m_cdv.SetNames( "Currency Trader", m_pWatch->GetInstrumentName() );

  {
    pPosition_t pPosition = f( pWatch, pWatch->GetInstrumentName() +":up" );
    assert( pPosition );
    m_to_up.Set( pPosition, m_cdv, EChartSlot::Price );
  }

  {
    pPosition_t pPosition = f( pWatch, pWatch->GetInstrumentName() +":dn" );
    assert( pPosition );
    m_to_dn.Set( pPosition, m_cdv, EChartSlot::Price );
  }

  //m_pWatch->RecordSeries( false );
  m_pWatch->OnQuote.Add( MakeDelegate( this, &Strategy::HandleQuote ) );
  m_pWatch->OnTrade.Add( MakeDelegate( this, &Strategy::HandleTrade ) );

}

Strategy::pairBidAsk_t Strategy::LatestQuote() const {
  return pairBidAsk_t( m_quote.Bid(), m_quote.Ask() );
}

void Strategy::HandleQuote( const ou::tf::Quote& quote ) {

  m_quote = quote;

  const ptime dt( m_quote.DateTime() );
  const double ask( m_quote.Ask() );
  const double bid( m_quote.Bid() );

  m_ceQuoteAsk.Append( dt, ask );
  m_ceQuoteBid.Append( dt, bid );

 if ( ask < m_trFalling.dblExtension ) m_trFalling.dblExtension = ask;
 if ( bid > m_trRising.dblExtension ) m_trRising.dblExtension = bid;

  //m_quotes.Append( quote );

  //TimeTick( quote );

  m_bfQuotes01Sec.Add( dt, m_quote.Midpoint(), 1 ); // provides a 1 sec pulse for checking the algorithm

}

void Strategy::HandleTrade( const ou::tf::Trade& trade ) {

  const ptime dt( trade.DateTime() );
  //const ou::tf::Trade::price_t  price(  trade.Price() );
  const ou::tf::Trade::volume_t volume( trade.Volume() );

  // m_ceTrade.Append( dt, price ); // for iqfeed, is same as bid price for fxcm
  const double fake_price( m_quote.Midpoint() );
  m_ceTrade.Append( dt, fake_price );
  //m_ceVolume.Append( dt, volume ); // iqfeed shows as 1 for fxcm

  m_bfTrading.Add( dt, fake_price, volume );

}

void Strategy::HandleBellHeard( boost::gregorian::date, boost::posix_time::time_duration ) {

  // one time calc pip
  const double mid( m_quote.Midpoint() );
  m_tick = m_to_up.PriceInterval( mid );
  const double first = (double)m_quantityToOrder * m_tick;

  double second {};

  switch ( m_eBaseCurrency ) {
    case EBase::First:
      second = first * mid;
      m_base_currency_pip = first;
      break;
    case EBase::Second:
      second = first / mid;
      m_base_currency_pip = second;
      break;
    default:
      assert( false );
  }
  BOOST_LOG_TRIVIAL(info)
           << "pip"
    << ',' << m_pWatch->GetInstrumentName()
    << ',' << "midprice=" << mid
    << ',' << "interval=" << m_tick
    << '.' << "first=" << first
    << ',' << "second=" << second
    << ',' << "quan=" << m_quantityToOrder
    ;
}

void Strategy::HandleRHTrading( const ou::tf::Bar& bar ) { // once a second

  const auto dt( bar.DateTime() );

  double unrealized, realized, commission, total;

  m_to_up.QueryStats( unrealized, realized, commission, total );
  m_plUp.m_ceUnRealized.Append( dt, unrealized );
  m_plUp.m_ceRealized.Append( dt, realized );
  m_plUp.m_ceProfitLoss.Append( dt, commission );
  m_plUp.m_ceCommission.Append( dt, total );

  m_pPortfolio->QueryStats( unrealized, realized, commission, total );
  m_plTtl.m_ceUnRealized.Append( dt, unrealized );
  m_plTtl.m_ceRealized.Append( dt, realized );
  m_plTtl.m_ceProfitLoss.Append( dt, commission );
  m_plTtl.m_ceCommission.Append( dt, total );

  m_to_dn.QueryStats( unrealized, realized, commission, total );
  m_plDn.m_ceUnRealized.Append( dt, unrealized );
  m_plDn.m_ceRealized.Append( dt, realized );
  m_plDn.m_ceProfitLoss.Append( dt, commission );
  m_plDn.m_ceCommission.Append( dt, total );

  static const double count( 4.0 );
  static const double count_base( count - 1.0 );
  static const double exist( count_base/count );
  static const double next( 1.0/count );

  const double ask( m_quote.Ask() );
  const double bid( m_quote.Bid() );

  switch ( m_state.swing ) {
    case State::Swing::up:
      m_state.sum += ( m_state.last - ask );
      m_state.last = ask;

      m_trFalling.dblEma = exist * m_trFalling.dblEma + next * ( m_trFalling.dblStart - m_trFalling.dblExtension );
      m_trFalling.dblStart = m_trFalling.dblExtension = bid;

      m_ceTradingRangeFalling.Append( dt, m_trFalling.dblEma );

      m_trRising.dblStart = m_trRising.dblExtension = ask;
      break;
    case State::Swing::none:
      break;
    case State::Swing::down:
      m_state.sum += ( bid - m_state.last );
      m_state.last = bid;

      m_trRising.dblEma = exist * m_trRising.dblEma + next * ( m_trRising.dblExtension - m_trRising.dblStart );
      m_trRising.dblStart = m_trRising.dblExtension = ask;

      m_ceTradingRangeRising.Append( dt, m_trRising.dblEma );

      m_trFalling.dblStart = m_trFalling.dblExtension = bid;
      break;
  }

  // things to check:
  //   track ATR at swingup/swingdn
  //   check distance from ema, if large correction, don't trade, if a few ATRs away, then consider trading

  RunStateUp( m_to_up );
  RunStateDn( m_to_dn );

  m_state.swing = State::Swing::none;
}

bool Strategy::SwingBarState( const Swing::EBarState eBarState ) const {
  // unused, eliminated too many signals
  return
       ( eBarState == m_rSwing[4].eBarState )
    && ( eBarState == m_rSwing[3].eBarState )
    && ( eBarState == m_rSwing[2].eBarState )
    && ( eBarState == m_rSwing[1].eBarState )
    && ( eBarState == m_rSwing[0].eBarState )
    ;
}

void Strategy::RunStateUp( TrackOrder& to ) {
  switch ( to.m_stateTrade ) {
    case TrackOrder::ETradeState::Init: // Strategy starts in this state
      to.m_stateTrade = TrackOrder::ETradeState::Search;
      break;
    case TrackOrder::ETradeState::Search:
      switch ( m_state.swing ) {
        case State::Swing::up:
          if ( m_pEmaCurrency->dblEmaLatest > m_quote.Ask() ) {
            to.Set(
              [this]( double fill_price ){
                m_stopUp.diff = fill_price - m_stopUp.start;
                assert( 0.0 < m_stopUp.diff );
              });
            m_stopUp.trail = m_stopUp.start = m_rSwing[ 2 ].lo; // run a parabolic stop?
            m_stopUp.diff = m_quote.Bid() - m_stopUp.trail; // preliminary initialization
            BOOST_LOG_TRIVIAL(info)
              << m_pWatch->GetInstrumentName() << ','
              << "up" << ','
              << "b=" << m_quote.Bid() << ','
              << "a=" << m_quote.Ask() << ','
              << "trf=" << m_TRFast.true_range << ','
              << "trs=" << m_TRSlow.true_range << ','
              << "sw=" << m_rSwing[0].lo << ',' << m_rSwing[1].lo << ',' << m_rSwing[2].lo << ',' << m_rSwing[3].lo << ',' << m_rSwing[4].lo << ','
              << "st=" << m_stopUp.start << ','
              << "df=" << m_stopUp.diff << ','
              << "trl=" << m_stopUp.trail
              ;
            assert( 0.0 < m_stopUp.diff );
            to.EnterLongLmt( TrackOrder::OrderArgs( m_quote.DateTime(), m_quote.Ask(), m_quote.Bid() ) );
          }
          break;
        case State::Swing::none:
          break;
        case State::Swing::down:
          break;
      }
      break;
    case TrackOrder::ETradeState::EntrySubmitted:
      // wait for exectuion
      break;
    case TrackOrder::ETradeState::ExitSignal:
      switch ( m_state.swing ) {
        case State::Swing::up:
          break;
        case State::Swing::none:
          {
            const double bid = m_quote.Bid();
            if ( bid <= m_stopUp.trail ) {
              // exit with stop
              BOOST_LOG_TRIVIAL(info)
                << m_pWatch->GetInstrumentName() << ','
                << "up exit on none"
                ;
              to.ExitLongMkt( TrackOrder::OrderArgs( m_quote.DateTime(), bid ) );
            }
            else {
              // update trailing stop
              const double diff = bid - m_stopUp.trail;
              if ( diff > m_stopUp.diff ) {
                m_stopUp.trail = bid - m_stopUp.diff;
              }
            }
          }
          break;
        case State::Swing::down:
          // should be exiting earlier than this
          BOOST_LOG_TRIVIAL(info)
            << m_pWatch->GetInstrumentName() << ','
            << "up exit on down"
            ;
          to.ExitLongMkt( TrackOrder::OrderArgs( m_quote.DateTime(), m_quote.Midpoint() ) );
          break;
      }
      break;
    case TrackOrder::ETradeState::ExitSubmitted:
      // wait for execution
      break;
    case TrackOrder::ETradeState::NoTrade:
      break;
    case TrackOrder::ETradeState::EndOfDayCancel:
      break;
    case TrackOrder::ETradeState::EndOfDayNeutral:
      break;
    case TrackOrder::ETradeState::Done:
      break;
    default:
      assert( false );
  }
}

void Strategy::RunStateDn( TrackOrder& to ) {
  switch ( to.m_stateTrade ) {
    case TrackOrder::ETradeState::Init: // Strategy starts in this state
      to.m_stateTrade = TrackOrder::ETradeState::Search;
      break;
    case TrackOrder::ETradeState::Search:
      switch ( m_state.swing ) {
        case State::Swing::up:
          break;
        case State::Swing::none:
          break;
        case State::Swing::down:
          if ( m_pEmaCurrency->dblEmaLatest < m_quote.Bid() ) {
            to.Set(
              [this]( double fill_price ){
                m_stopDn.diff = m_stopDn.start - fill_price;
                assert( 0.0 < m_stopDn.diff );
              });
            m_stopDn.trail = m_stopDn.start = m_rSwing[ 2 ].hi; // run a parabolic stop?
            m_stopDn.diff = m_stopDn.trail - m_quote.Ask();
            BOOST_LOG_TRIVIAL(info)
              << m_pWatch->GetInstrumentName() << ','
              << "dn" << ','
              << "b=" << m_quote.Bid() << ','
              << "a=" << m_quote.Ask() << ','
              << "trf=" << m_TRFast.true_range << ','
              << "trs=" << m_TRSlow.true_range << ','
              << "sw=" << m_rSwing[0].hi << ',' << m_rSwing[1].hi << ',' << m_rSwing[2].hi << ',' << m_rSwing[3].hi << ',' << m_rSwing[4].hi << ','
              << "st=" << m_stopDn.start << ','
              << "df=" << m_stopDn.diff << ','
              << "trl=" << m_stopDn.trail
              ;
            assert( 0.0 < m_stopDn.diff );
            to.EnterShortLmt( TrackOrder::OrderArgs( m_quote.DateTime(), m_quote.Bid(), m_quote.Ask() ) );
          }
          break;
      }
      break;
    case TrackOrder::ETradeState::EntrySubmitted:
      // wait for exectuion
      break;
    case TrackOrder::ETradeState::ExitSignal:
      switch ( m_state.swing ) {
        case State::Swing::up:
          // should be exiting earlier than this
          BOOST_LOG_TRIVIAL(info)
            << m_pWatch->GetInstrumentName() << ','
            << "dn exit up"
            ;
          to.ExitShortMkt( TrackOrder::OrderArgs( m_quote.DateTime(), m_quote.Midpoint() ) );
          break;
        case State::Swing::none:
          {
            const double ask = m_quote.Ask();
            if ( ask >= m_stopDn.trail ) {
              // exit with stop
              BOOST_LOG_TRIVIAL(info)
                << m_pWatch->GetInstrumentName() << ','
                << "dn exit none"
                ;
              to.ExitShortMkt( TrackOrder::OrderArgs( m_quote.DateTime(), ask ) );
            }
            else {
              // update trailing stop
              const double diff = m_stopDn.trail - ask;
              if ( diff > m_stopDn.diff ) {
                m_stopDn.trail = m_stopDn.diff - ask;
              }
            }
          }
          break;
        case State::Swing::down:
          break;
      }
      break;
    case TrackOrder::ETradeState::ExitSubmitted:
      // wait for execution
      break;
    case TrackOrder::ETradeState::NoTrade:
      break;
    case TrackOrder::ETradeState::EndOfDayCancel:
      break;
    case TrackOrder::ETradeState::EndOfDayNeutral:
      break;
    case TrackOrder::ETradeState::Done:
      break;
    default:
      assert( false );
  }
}

void Strategy::HandleCancel( boost::gregorian::date date, boost::posix_time::time_duration td ) { // one shot
  m_to_up.HandleCancel( date, td );
  m_to_dn.HandleCancel( date, td );
}

void Strategy::HandleGoNeutral( boost::gregorian::date date, boost::posix_time::time_duration td ) { // one shot
  m_to_up.HandleGoNeutral( date, td );
  m_to_dn.HandleGoNeutral( date, td );
}

void Strategy::HandleAtRHClose( boost::gregorian::date date, boost::posix_time::time_duration time ) { // one shot
  BOOST_LOG_TRIVIAL(info)
    << m_pWatch->GetInstrumentName()
    << " swing delta " << m_nNet
    << ", sum " << m_state.sum
    << ", " << date << 'T' << time
    ;
}

void Strategy::HandleBarQuotes01Sec( const ou::tf::Bar& bar ) {
  assert( m_pEmaCurrency );
  m_pEmaCurrency->Update( bar.DateTime(), bar.Close() );
  TimeTick( bar );
}

void Strategy::HandleMinuteBar( const ou::tf::Bar& bar ) {

  const ptime dt( bar.DateTime() );
  m_ceBarsTrade.AppendBar( bar );

  m_ceVolume.Append( dt, bar.Volume() ); // iqfeed shows as 1 for fxcm

  m_pATRFast->Update( dt, m_TRFast.Update( bar ) );
  m_pATRSlow->Update( dt, m_TRSlow.Update( bar ) );

  // calculate swing points
  Swing& a( m_rSwing[ 0 ] );
  Swing& b( m_rSwing[ 1 ] );
  Swing& c( m_rSwing[ 2 ] );
  Swing& d( m_rSwing[ 3 ] );
  Swing& e( m_rSwing[ 4 ] );

  a = b; b = c; c = d; d = e; e.Update( bar, m_pEmaCurrency->dblEmaLatest );

  { // highest point
    const double x = a.hi > b.hi ? a.hi : b.hi;
    const double y = d.hi > e.hi ? d.hi : e.hi;
    const double z = x > y ? x : y;
    if ( c.hi > z ) {
      m_ceSwingLo.AddLabel( c.dt, c.hi, "Swing Dn" );
      m_vSwingTrack.emplace_back( SwingTrack(
        bar.DateTime(),
        c.dt, c.hi,
        e.dt, e.hi ) );
      m_nHi++;
      m_nNet++;
      m_state.swing = State::Swing::down;
      //std::cout
      //         << m_pWatch->GetInstrumentName()
      //  << ',' << "hi"
      //  << ',' << c.hi << ',' << e.hi
      //  << std::endl;
    }
  }

  { // lowest point
    const double x = a.lo < b.lo ? a.lo : b.lo;
    const double y = d.lo < e.lo ? d.lo : e.lo;
    const double z = x < y ? x : y;
    if ( c.lo < z ) {
      m_ceSwingHi.AddLabel( c.dt, c.lo, "Swing Up" );
      m_vSwingTrack.emplace_back( SwingTrack(
        bar.DateTime(),
        c.dt, c.lo,
        e.dt, e.lo ) );
      m_nLo++;
      m_nNet--;
      m_state.swing = State::Swing::up;
      //std::cout
      //         << m_pWatch->GetInstrumentName()
      //  << ',' << "lo"
      //  << ',' << c.lo << ',' << e.lo
      //  << std::endl;
    }
  }

}

bool Strategy::HandleSoftwareReset( const ou::tf::DatedDatum& ) {
  // injected by external one second timer
  // clean up, clear out, start new statistics?
  if ( nullptr != m_fResetSoftware ) {
    return m_fResetSoftware();
  }
  else {
    return false;
  }
}

void Strategy::SaveWatch( const std::string& sPrefix ) {
  if ( m_pWatch ) {
    m_pWatch->SaveSeries( sPrefix );
    m_pWatch->ClearSeries();

    m_ceQuoteAsk.Clear();
    m_ceQuoteBid.Clear();
    m_ceTrade.Clear();
    m_ceVolume.Clear();

    m_plUp.Clear();
    m_plTtl.Clear();
    m_plDn.Clear();

    m_ceSwingHi.Clear();
    m_ceSwingLo.Clear();

    m_ceTradingRangeRising.Clear();
    m_ceTradingRangeFalling.Clear();
  }
}

void Strategy::CloseAndDone() {

}

  // The ATR is commonly used as an exit method that can be applied
  // no matter how the entry decision is made. One popular technique
  // is known as the "chandelier exit" and was developed by Chuck LeBeau:
  //   place a trailing stop under the highest high the stock has reached
  //   since you entered the trade. The distance between the highest high
  //   and the stop level is defined as some multiple multiplied by the ATR.

  // While the ATR doesn't tell us in which direction the breakout will occur,
  // it can be added to the closing price, and the trader can buy whenever
  // the next day's price trades above that value.

  // ATR can be used to set up entry signals.
  // ATR breakout systems are commonly used by short-term traders to time entries.
  // This system adds the ATR, or a multiple of the ATR, to the next day's open
  // and buys when prices move above that level. Short trades are the opposite;
  // the ATR or a multiple of the ATR is subtracted from the open and entries
  // occur when that level is broken.

  // maybe ATR could be connected with Darvis for timing entries.

  // To adjust for volatility you can take 10 000 (or whatever number you set as default)
  // and divide by the Average True Range over the last 100 days.
  // This way you allocate more capital to the least volatile stocks compared to the ones more volatile.

  // https://tradeciety.com/how-to-use-the-atr-indicator-guide for Kelner bands
  // ATR can be used to calculate typical PIP movement per day for trading volatility
  // use to calculate daily ATR for an idea of daily trading ranges

  // Timing Trades: ATR is crucial in deciding when to execute trades.
  // A high ATR, signaling high volatility, suggests larger price fluctuations and may be suitable
  // for breakout strategies or anticipating major market shifts.
  // A low ATR, indicating a quieter market, may favor strategies suited to a range-bound environment.

  // Setting Stop Losses: In determining stop-loss orders, ATR is invaluable.
  // It helps traders set stop losses that reflect current market volatility.
  // In a high-volatility (high ATR) scenario, a larger stop loss might be necessary
  // to avoid early exits due to market volatility. In contrast, in low-volatility (low ATR) markets,
  // tighter stop losses could be more effective, minimizing losses while
  // preventing unnecessary position exits.

  // Risk Management: ATR assists in risk management by providing insights into market volatility.
  // Traders can adjust their position sizes in response to this.
  // In volatile markets, reducing position sizes helps manage risk,
  // while in more stable conditions, larger positions might be beneficial.

  // Market Sentiment: ATR can also offer indirect indications of market sentiment.
  // A rapid increase in ATR may signal growing market tension or excitement,
  // potentially indicating upcoming significant market movements.
  // A gradual decrease in ATR can suggest a stabilizing market,
  // often leading to a consolidation phase.

  // ATR can be instrumental in weighing a limit or a market order.
  // The volatility measured by ATR can guide traders in deciding
  // whether to opt for a limit order, seeking a specific price point,
  // or a market order, executing a trade at the current market price,
  // depending on the market’s perceived stability or volatility.
