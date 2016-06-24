/*
  Copyright 2012 SINTEF ICT, Applied Mathematics.

  This file is part of the Open Porous Media project (OPM).

  OPM is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  OPM is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with OPM.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef OPM_WELLSTATE_HEADER_INCLUDED
#define OPM_WELLSTATE_HEADER_INCLUDED

#include <opm/core/wells.h>
#include <opm/core/well_controls.h>
#include <opm/output/Wells.hpp>

#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <cassert>
#include <cstddef>

namespace Opm
{

    /// The state of a set of wells.
    class WellState
    {
    public:
        typedef std::array< int, 3 >  mapentry_t;
        typedef std::map< std::string, mapentry_t > WellMapType;

        /// Allocate and initialize if wells is non-null.
        /// Also tries to give useful initial values to the bhp() and
        /// wellRates() fields, depending on controls.  The
        /// perfRates() field is filled with zero, and perfPress()
        /// with -1e100.
        template <class State>
        void init(const Wells* wells, const State& state)
        {
            // clear old name mapping
            wellMap_.clear();
            wells_.reset( clone_wells( wells ) );

            if (wells) {
                const int nw = wells->number_of_wells;
                numPhases_ = wells->number_of_phases;
                bhp_.resize(nw);
                thp_.resize(nw);
                temperature_.resize(nw, 273.15 + 20); // standard temperature for now
                wellrates_.resize(nw * numPhases_, 0.0);
                for (int w = 0; w < nw; ++w) {
                    assert((wells->type[w] == INJECTOR) || (wells->type[w] == PRODUCER));
                    const WellControls* ctrl = wells->ctrls[w];

                    // setup wellname -> well index mapping
                    {
                        assert( wells->name[ w ] );
                        std::string name( wells->name[ w ] );
                        assert( name.size() > 0 );
                        mapentry_t& wellMapEntry = wellMap_[name];
                        wellMapEntry[ 0 ] = w;
                        wellMapEntry[ 1 ] = wells->well_connpos[w];
                        // also store the number of perforations in this well
                        const int num_perf_this_well = wells->well_connpos[w + 1] - wells->well_connpos[w];
                        wellMapEntry[ 2 ] = num_perf_this_well;
                    }

                    if (well_controls_well_is_stopped(ctrl)) {
                        // Stopped well:
                        // 1. Rates: assign zero well rates.
                        for (int p = 0; p < numPhases_; ++p) {
                            wellrates_[numPhases_*w + p] = 0.0;
                        }
                        // 2. Bhp: assign bhp equal to bhp control, if
                        //    applicable, otherwise assign equal to
                        //    first perforation cell pressure.
                        if (well_controls_get_current_type(ctrl) == BHP) {
                            bhp_[w] = well_controls_get_current_target( ctrl );
                        } else {
                            const int first_cell = wells->well_cells[wells->well_connpos[w]];
                            bhp_[w] = state.pressure()[first_cell];
                        }
                        // 3. Thp: assign thp equal to thp control, if applicable,
                        //    otherwise assign equal to bhp value.
                        if (well_controls_get_current_type(ctrl) == THP) {
                            thp_[w] = well_controls_get_current_target( ctrl );
                        } else {
                            thp_[w] = bhp_[w];
                        }
                    } else {
                        // Open well:
                        // 1. Rates: initialize well rates to match controls
                        //    if type is SURFACE_RATE.  Otherwise, we
                        //    cannot set the correct value here, so we
                        //    assign a small rate with the correct
                        //    sign so that any logic depending on that
                        //    sign will work as expected.
                        if (well_controls_get_current_type(ctrl) == SURFACE_RATE) {
                            const double rate_target = well_controls_get_current_target(ctrl);
                            const double * distr = well_controls_get_current_distr( ctrl );
                            for (int p = 0; p < numPhases_; ++p) {
                                wellrates_[numPhases_*w + p] = rate_target * distr[p];
                            }
                        } else {
                            const double small_rate = 1e-14;
                            const double sign = (wells->type[w] == INJECTOR) ? 1.0 : -1.0;
                            for (int p = 0; p < numPhases_; ++p) {
                                wellrates_[numPhases_*w + p] = small_rate * sign;
                            }
                        }

                        // 2. Bhp: initialize bhp to be target pressure if
                        //    bhp-controlled well, otherwise set to a
                        //    little above or below (depending on if
                        //    the well is an injector or producer)
                        //    pressure in first perforation cell.
                                    if (well_controls_get_current_type(ctrl) == BHP) {
                                        bhp_[w] = well_controls_get_current_target( ctrl );
                                    } else {
                                        const int first_cell = wells->well_cells[wells->well_connpos[w]];
                                        const double safety_factor = (wells->type[w] == INJECTOR) ? 1.01 : 0.99;
                                        bhp_[w] = safety_factor*state.pressure()[first_cell];
                        }

                        // 3. Thp: assign thp equal to thp control, if applicable,
                        //    otherwise assign equal to bhp value.
                                    if (well_controls_get_current_type(ctrl) == THP) {
                                        thp_[w] = well_controls_get_current_target( ctrl );
                                    } else {
                                        thp_[w] = bhp_[w];
                        }
                        }
                }

                // The perforation rates and perforation pressures are
                // not expected to be consistent with bhp_ and wellrates_
                // after init().
                perfrates_.resize(wells->well_connpos[nw], 0.0);
                perfpress_.resize(wells->well_connpos[nw], -1e100);
            }
        }

        /// One bhp pressure per well.
        std::vector<double>& bhp() { return bhp_; }
        const std::vector<double>& bhp() const { return bhp_; }

        /// One thp pressure per well.
        std::vector<double>& thp() { return thp_; }
        const std::vector<double>& thp() const { return thp_; }

        /// One temperature per well.
        std::vector<double>& temperature() { return temperature_; }
        const std::vector<double>& temperature() const { return temperature_; }

        /// One rate per well and phase.
        std::vector<double>& wellRates() { return wellrates_; }
        const std::vector<double>& wellRates() const { return wellrates_; }

        /// One rate per well connection.
        std::vector<double>& perfRates() { return perfrates_; }
        const std::vector<double>& perfRates() const { return perfrates_; }

        /// One pressure per well connection.
        std::vector<double>& perfPress() { return perfpress_; }
        const std::vector<double>& perfPress() const { return perfpress_; }

        size_t getRestartBhpOffset() const {
            return 0;
        }

        size_t getRestartPerfPressOffset() const {
            return bhp_.size();
        }

        size_t getRestartPerfRatesOffset() const {
            return getRestartPerfPressOffset() + perfpress_.size();
        }

        size_t getRestartTemperatureOffset() const {
            return getRestartPerfRatesOffset() + perfrates_.size();
        }

        size_t getRestartWellRatesOffset() const {
            return getRestartTemperatureOffset() + temperature_.size();
        }

        const WellMapType& wellMap() const { return wellMap_; }
        WellMapType& wellMap() { return wellMap_; }

        /// The number of wells present.
        int numWells() const
        {
            return bhp().size();
        }

        /// The number of phases present.
        int numPhases() const
        {
            return numPhases_;
        }

        virtual data::Wells report() const
        {
            return { { /* WellState offers no completion data, so that has to be added later */ },
                this->bhp(),
                this->temperature(),
                this->wellRates(),
                this->perfPress(),
                this->perfRates() };
        }

        virtual ~WellState() {}

        WellState() = default;
        WellState( const WellState& rhs ) :
            bhp_( rhs.bhp_ ),
            thp_( rhs.thp_ ),
            temperature_( rhs.temperature_ ),
            wellrates_( rhs.wellrates_ ),
            perfrates_( rhs.perfrates_ ),
            perfpress_( rhs.perfpress_ ),
	    numPhases_( rhs.numPhases_ ),
            wellMap_( rhs.wellMap_ ),
            wells_( clone_wells( rhs.wells_.get() ) )
        {}

        WellState& operator=( const WellState& rhs ) {
            this->bhp_ = rhs.bhp_;
            this->thp_ = rhs.thp_;
            this->temperature_ = rhs.temperature_;
            this->wellrates_ = rhs.wellrates_;
            this->perfrates_ = rhs.perfrates_;
            this->perfpress_ = rhs.perfpress_;
            this->wellMap_ = rhs.wellMap_;
            this->wells_.reset( clone_wells( rhs.wells_.get() ) );

            return *this;
        }

    private:
        std::vector<double> bhp_;
        std::vector<double> thp_;
        std::vector<double> temperature_;
        std::vector<double> wellrates_;
        std::vector<double> perfrates_;
        std::vector<double> perfpress_;
        int numPhases_;

        WellMapType wellMap_;

    protected:
        struct wdel {
            void operator()( Wells* w ) { destroy_wells( w ); }
        };
        std::unique_ptr< Wells, wdel > wells_;
    };

} // namespace Opm

#endif // OPM_WELLSTATE_HEADER_INCLUDED
