/*****************************************************************************
 *
 * This file is part of Mapnik (c++ mapping toolkit)
 *
 * Copyright (C) 2011 Artem Pavlenko
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 *****************************************************************************/

#ifndef PGRASTER_FEATURESET_HPP
#define PGRASTER_FEATURESET_HPP

// mapnik
#include <mapnik/feature.hpp>

// boost
#include <boost/variant.hpp>
#include <boost/optional.hpp>

#include "pgraster_datasource.hpp"

class GDALDataset;
class GDALRasterBand;

typedef boost::variant<mapnik::query, mapnik::coord2d> pgraster_query;

class pgraster_featureset : public mapnik::Featureset
{
    struct query_dispatch : public boost::static_visitor<mapnik::feature_ptr>
    {
        query_dispatch( pgraster_featureset & featureset)
            : featureset_(featureset) {}

        mapnik::feature_ptr operator() (mapnik::query const& q) const
        {
            return featureset_.get_feature(q);
        }

        mapnik::feature_ptr operator() (mapnik::coord2d const& p) const
        {
            return featureset_.get_feature_at_point(p);
        }

        pgraster_featureset & featureset_;
    };

public:
    pgraster_featureset(GDALDataset& dataset,
                    int band,
                    pgraster_query q,
                    mapnik::box2d<double> extent,
                    unsigned width,
                    unsigned height,
                    int nbands,
                    double dx,
                    double dy,
                    boost::optional<double> const& nodata,
                    double nodata_tolerance);
    virtual ~pgraster_featureset();
    mapnik::feature_ptr next();

private:
    mapnik::feature_ptr get_feature(mapnik::query const& q);
    mapnik::feature_ptr get_feature_at_point(mapnik::coord2d const& p);

#ifdef MAPNIK_LOG
    void get_overview_meta(GDALRasterBand * band);
#endif

    GDALDataset & dataset_;
    mapnik::context_ptr ctx_;
    int band_;
    pgraster_query gquery_;
    mapnik::box2d<double> raster_extent_;
    unsigned raster_width_;
    unsigned raster_height_;
    double dx_;
    double dy_;
    int nbands_;
    boost::optional<double> nodata_value_;
    double nodata_tolerance_;
    bool first_;
};

#endif // PGRASTER_FEATURESET_HPP
