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

#include "pgraster_datasource.hpp"
#include "pgraster_featureset.hpp"

// mapnik
#include <mapnik/debug.hpp>
#include <mapnik/boolean.hpp>
#include <mapnik/geom_util.hpp>
#include <mapnik/timer.hpp>
#include <mapnik/value_types.hpp>

// gdal
#include <gdal_version.h>

using mapnik::datasource;
using mapnik::parameters;

DATASOURCE_PLUGIN(pgraster_datasource)

using mapnik::box2d;
using mapnik::coord2d;
using mapnik::query;
using mapnik::featureset_ptr;
using mapnik::layer_descriptor;
using mapnik::datasource_exception;


/*
 * Opens a GDALDataset and returns a pointer to it.
 * Caller is responsible for calling GDALClose on it
 */
inline GDALDataset* pgraster_datasource::open_dataset() const
{
    MAPNIK_LOG_DEBUG(gdal) << "pgraster_datasource: Opening " << dataset_name_;

    GDALDataset *dataset;
#if GDAL_VERSION_NUM >= 1600
    if (shared_dataset_)
    {
        dataset = reinterpret_cast<GDALDataset*>(GDALOpenShared((dataset_name_).c_str(), GA_ReadOnly));
    }
    else
#endif
    {
        dataset = reinterpret_cast<GDALDataset*>(GDALOpen((dataset_name_).c_str(), GA_ReadOnly));
    }

    if (! dataset)
    {
        throw datasource_exception(CPLGetLastErrorMsg());
    }

    return dataset;
}


pgraster_datasource::pgraster_datasource(parameters const& params)
    : datasource(params),
      desc_(*params.get<std::string>("type"), "utf-8"),
      nodata_value_(params.get<double>("nodata")),
      nodata_tolerance_(*params.get<double>("nodata_tolerance",1e-12))
{
    MAPNIK_LOG_DEBUG(gdal) << "pgraster_datasource: Initializing...";

#ifdef MAPNIK_STATS
    mapnik::progress_timer __stats__(std::clog, "pgraster_datasource::init");
#endif

    GDALAllRegister();

    boost::optional<std::string> host = params.get<std::string>("host");
    boost::optional<std::string> port = params.get<std::string>("port");
    boost::optional<std::string> dbname = params.get<std::string>("dbname");
    boost::optional<std::string> user = params.get<std::string>("user");
    boost::optional<std::string> password = params.get<std::string>("password");
    boost::optional<std::string> table = params.get<std::string>("table");
    boost::optional<std::string> column = params.get<std::string>("raster_field");

    dataset_name_ = "pg:mode=2";
    if ( dbname ) dataset_name_ += " dbname=" + dbname.get();
    if ( host ) dataset_name_ += " host=" + host.get();
    if ( user ) dataset_name_ += " user=" + user.get();
    if ( port ) dataset_name_ += " port=" + port.get();
    if ( table ) dataset_name_ += " table=" + table.get();
    else throw datasource_exception("missing <table> parameter");
    if ( column ) dataset_name_ += " column=" + *column;

    shared_dataset_ = *params.get<mapnik::boolean>("shared", false);
    band_ = *params.get<mapnik::value_integer>("band", -1);

    GDALDataset *dataset = open_dataset();

    nbands_ = dataset->GetRasterCount();
    width_ = dataset->GetRasterXSize();
    height_ = dataset->GetRasterYSize();
    desc_.add_descriptor(mapnik::attribute_descriptor("nodata", mapnik::Integer));

    double tr[6];
    bool bbox_override = false;
    boost::optional<std::string> bbox_s = params.get<std::string>("extent");
    if (bbox_s)
    {
        MAPNIK_LOG_DEBUG(gdal) << "pgraster_datasource: BBox Parameter=" << *bbox_s;

        bbox_override = extent_.from_string(*bbox_s);
        if (! bbox_override)
        {
            throw datasource_exception("PGRaster Plugin: bbox parameter '" + *bbox_s + "' invalid");
        }
    }

    if (bbox_override)
    {
        tr[0] = extent_.minx();
        tr[1] = extent_.width() / (double)width_;
        tr[2] = 0;
        tr[3] = extent_.maxy();
        tr[4] = 0;
        tr[5] = -extent_.height() / (double)height_;
        MAPNIK_LOG_DEBUG(gdal) << "pgraster_datasource extent override gives Geotransform="
                               << tr[0] << "," << tr[1] << ","
                               << tr[2] << "," << tr[3] << ","
                               << tr[4] << "," << tr[5];
    }
    else
    {
        if (dataset->GetGeoTransform(tr) != CPLE_None)
        {
            MAPNIK_LOG_DEBUG(gdal) << "pgraster_datasource GetGeotransform failure gives="
                                   << tr[0] << "," << tr[1] << ","
                                   << tr[2] << "," << tr[3] << ","
                                   << tr[4] << "," << tr[5];
        }
        else
        {
            MAPNIK_LOG_DEBUG(gdal) << "pgraster_datasource Geotransform="
                                   << tr[0] << "," << tr[1] << ","
                                   << tr[2] << "," << tr[3] << ","
                                   << tr[4] << "," << tr[5];
        }
    }

    // TODO - We should throw for true non-north up images, but the check
    // below is clearly too restrictive.
    // https://github.com/mapnik/mapnik/issues/970
    /*
      if (tr[2] != 0 || tr[4] != 0)
      {
      throw datasource_exception("PGRaster Plugin: only 'north up' images are supported");
      }
    */

    dx_ = tr[1];
    dy_ = tr[5];

    if (! bbox_override)
    {
        double x0 = tr[0];
        double y0 = tr[3];
        double x1 = tr[0] + width_ * dx_ + height_ *tr[2];
        double y1 = tr[3] + width_ *tr[4] + height_ * dy_;

        /*
          double x0 = tr[0] + (height_) * tr[2]; // minx
          double y0 = tr[3] + (height_) * tr[5]; // miny

          double x1 = tr[0] + (width_) * tr[1]; // maxx
          double y1 = tr[3] + (width_) * tr[4]; // maxy
        */

        extent_.init(x0, y0, x1, y1);
    }

    GDALClose(dataset);

    MAPNIK_LOG_DEBUG(gdal) << "pgraster_datasource: Raster Size=" << width_ << "," << height_;
    MAPNIK_LOG_DEBUG(gdal) << "pgraster_datasource: Raster Extent=" << extent_;

}

pgraster_datasource::~pgraster_datasource()
{
}

datasource::datasource_t pgraster_datasource::type() const
{
    return datasource::Raster;
}

const char * pgraster_datasource::name()
{
    return "pgraster";
}

box2d<double> pgraster_datasource::envelope() const
{
    return extent_;
}

boost::optional<mapnik::datasource::geometry_t> pgraster_datasource::get_geometry_type() const
{
    return boost::optional<mapnik::datasource::geometry_t>();
}

layer_descriptor pgraster_datasource::get_descriptor() const
{
    return desc_;
}

featureset_ptr pgraster_datasource::features(query const& q) const
{
#ifdef MAPNIK_STATS
    mapnik::progress_timer __stats__(std::clog, "pgraster_datasource::features");
#endif

    pgraster_query gq = q;

    // TODO - move to boost::make_shared, but must reduce # of args to <= 9
    return featureset_ptr(new pgraster_featureset(*open_dataset(),
                                              band_,
                                              gq,
                                              extent_,
                                              width_,
                                              height_,
                                              nbands_,
                                              dx_,
                                              dy_,
                                              nodata_value_,
                                              nodata_tolerance_));
}

featureset_ptr pgraster_datasource::features_at_point(coord2d const& pt, double tol) const
{
#ifdef MAPNIK_STATS
    mapnik::progress_timer __stats__(std::clog, "pgraster_datasource::features_at_point");
#endif

    pgraster_query gq = pt;

    // TODO - move to boost::make_shared, but must reduce # of args to <= 9
    return featureset_ptr(new pgraster_featureset(*open_dataset(),
                                              band_,
                                              gq,
                                              extent_,
                                              width_,
                                              height_,
                                              nbands_,
                                              dx_,
                                              dy_,
                                              nodata_value_,
                                              nodata_tolerance_));
}
