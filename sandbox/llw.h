/*
 * llw.h
 *
 *  Created on: Oct 13, 2010
 *      Author: cd
 *      Author: dc
 */

#ifndef LLW_H_
#define LLW_H_


#include <stdexcept>
#include <cmath>

#include <syslog.h>

#include <Eigen/LU>
#include <libconfig.h++>

#include <barrett/detail/stl_utils.h>
#include <barrett/math/utils.h>
#include <barrett/units.h>
#include <barrett/puck.h>
#include <barrett/puck_group.h>


namespace barrett {


class WamPuck {
public:
	WamPuck(Puck* puck = NULL) :
		cts(0), rpc(0.0), cpr(0.0), ipnm(0)
	{
		setPuck(puck);
	}

	void setPuck(Puck* puck, bool autoUpdate = true) {
		p = puck;
		if (p != NULL  &&  autoUpdate) {
			update();
		}
	}
	Puck* getPuck() const { return p; }
	void update() {
		cts = getProperty(Puck::CTS);
		rpc = 2*M_PI / cts;
		cpr = cts / (2*M_PI);

		ipnm = getProperty(Puck::IPNM);
	}

	int getProperty(enum Puck::Property prop) const {
		return p->getProperty(prop);
	}
	void setProperty(enum Puck::Property prop, int value) const {
		p->setProperty(prop, value);
	}

	int getId() const { return p->getId(); }
	int getVers() const { return p->getRole(); }
	int getRole() const { return p->getRole(); }

	int getCts() const { return cts; }
	double getRadsPerCount() const { return rpc; }
	double getCountsPerRad() const { return cpr; }
	double counts2rad(int counts) const { return rpc * counts; }
	int rad2counts(double rad) const { return floor(cpr * rad); }

	int getIpnm() const { return ipnm; }
	int nm2i(double torque) const { return floor(math::saturate(torque*ipnm, MAX_PUCK_TORQUE)); }


	static void sendPackedTorques(const CommunicationsBus& bus, int groupId, int propId,
			const double* pt, int numTorques) {
		unsigned char data[8];
		int tmp1, tmp2;

		// Special value-packing compilation: Packs (4) 14-bit values into 8 bytes
		//     0        1        2        3        4        5        6        7
		// ATPPPPPP AAAAAAaa aaaaaaBB BBBBbbbb bbbbCCCC CCcccccc ccDDDDDD dddddddd
		switch (numTorques) {
		case 4:
			tmp1 = floor(math::saturate(pt[3], MAX_PUCK_TORQUE));
			data[7] = static_cast<unsigned char>(tmp1 & 0x00FF);
		case 3:
			tmp2 = floor(math::saturate(pt[2], MAX_PUCK_TORQUE));
			data[6] = static_cast<unsigned char>( ((tmp2 << 6) & 0x00C0) | ((tmp1 >> 8) & 0x003F) );
			data[5] = static_cast<unsigned char>( ( tmp2 >> 2) & 0x00FF );
		case 2:
			tmp1 = floor(math::saturate(pt[1], MAX_PUCK_TORQUE));
			data[4] = static_cast<unsigned char>( ((tmp1 << 4) & 0x00F0) | ((tmp2 >> 10) & 0x000F) );
			data[3] = static_cast<unsigned char>( ( tmp1 >> 4) & 0x00FF );
		case 1:
			tmp2 = floor(math::saturate(pt[0], MAX_PUCK_TORQUE));
			data[2] = static_cast<unsigned char>( ((tmp2 << 2) & 0x00FC) | ((tmp1 >> 12) & 0x0003) );
			data[1] = static_cast<unsigned char>( ( tmp2 >> 6) & 0x00FF );
			break;

		default:
			throw std::logic_error("WamPuck::sendPackedTorques(): numTorques must be >= 1 and <= PUCKS_PER_TORQUE_GROUP.");
			break;
		}
		data[0] = propId | Puck::SET_MASK;

		bus.send(Puck::nodeId2BusId(groupId), data, (numTorques == 4) ? 8 : (1 + numTorques*2));
	}


	static const size_t PUCKS_PER_TORQUE_GROUP = 4;
	static const int MAX_PUCK_TORQUE = 8191;

protected:
	Puck* p;

	int cts;
	double rpc, cpr;
	int ipnm;

};


template<size_t DOF>
class LLW {
	BARRETT_UNITS_TEMPLATE_TYPEDEFS(DOF);

public:
	// genericPucks must be ordered by joint and must break into torque groups as arranged
	LLW(const std::vector<Puck*>& genericPucks, Puck* _safetyPuck, const libconfig::Setting& setting, std::vector<int> torqueGroupIds = std::vector<int>()) :
		bus(genericPucks[0]->getBus()), pucks(DOF), safetyPuck(_safetyPuck), wamGroup(PuckGroup::BGRP_WAM, genericPucks), torqueGroups(), home(setting["home"]), j2mp(setting["j2mp"]), torquePropId(-1)
	{
		syslog(LOG_ERR, "LLW::LLW(%s => \"%s\")", setting.getSourceFile(), setting.getPath().c_str());

		// Check number of Pucks
		if (genericPucks.size() != DOF) {
			syslog(LOG_ERR, "  Expected a vector of %d Pucks, got %d", DOF, pucks.size());
			throw std::invalid_argument("LLW::LLW(): Wrong number of Pucks. Check /var/log/syslog for details.");
		}

		// Zero-compensation?
		bool zeroCompensation = setting.exists("zeroangle");
		if (!zeroCompensation) {
			syslog(LOG_ERR, "  Missing \"zeroangle\" vector: no zero-compensation");
		}

		// Compute motor/joint transforms
		Eigen::LU<typename sqm_type::Base> lu(j2mp);
		if (!lu.isInvertible()) {
			syslog(LOG_ERR, "  j2mp matrix is not invertible");
			throw std::runtime_error("LLW::LLW(): j2mp matrix is not invertible.");
		}
		lu.computeInverse(&m2jp);
		j2mt = m2jp.transpose();


		// Initialize WamPucks
		Puck::wake(genericPucks);  // Make sure Pucks are awake
		for (size_t i = 0; i < DOF; ++i) {
			pucks[i].setPuck(genericPucks[i]);
		}

		// Setup PuckGroups
		// If no IDs are provided, use the defaults
		if (torqueGroupIds.size() == 0) {
			torqueGroupIds.push_back(PuckGroup::BGRP_LOWER_WAM);
			torqueGroupIds.push_back(PuckGroup::BGRP_UPPER_WAM);
		}
		size_t numTorqueGroups = ceil(static_cast<double>(DOF)/WamPuck::PUCKS_PER_TORQUE_GROUP);
		if (numTorqueGroups > torqueGroupIds.size()) {
			syslog(LOG_ERR, "  Need %d torque groups, only %d IDs provided", numTorqueGroups, torqueGroupIds.size());
			throw std::logic_error("LLW::LLW(): Too few torque group IDs provided. Check /var/log/syslog for details.");
		}

		size_t i = 0;
		for (size_t g = 0; g < numTorqueGroups; ++g) {
			std::vector<Puck*> tgPucks;
			while (tgPucks.size() < 4  &&  i < DOF) {
				tgPucks.push_back(genericPucks[i]);
				++i;
			}
			torqueGroups.push_back(new PuckGroup(torqueGroupIds[g], tgPucks));
		}

		// Verify properties
		if (wamGroup.verifyProperty(Puck::T)) {
			torquePropId = wamGroup.getPropertyId(Puck::T);
		} else {
			throw std::runtime_error("LLW::LLW(): WAM Pucks do not do not all have the same propId for the T property. Some Pucks might be a) in Monitor, b) have incompatible firmware versions, or c) have incompatible ROLEs.");
		}


		// Compute puck/joint transforms
		v_type cpr;
		for (size_t i = 0; i < DOF; ++i) {
			cpr[i] = pucks[i].getCountsPerRad();
		}
		j2pp = cpr.asDiagonal() * j2mp;

		v_type ipnm;
		for (size_t i = 0; i < DOF; ++i) {
			ipnm[i] = pucks[i].getIpnm();
		}
		j2pt = ipnm.asDiagonal() * j2mt;


		// Zero the WAM?
		if (safetyPuck == NULL) {
			syslog(LOG_ERR, "  No safetyPuck: WAM may not be zeroed");
		} else if (safetyPuck->getProperty(Puck::ZERO)) {
			syslog(LOG_ERR, "  WAM was already zeroed");
		} else if (zeroCompensation) {
			v_type zeroAngle(setting["zeroangle"]);

			v_type currentAngle;
			for (size_t i = 0; i < DOF; ++i) {
				currentAngle[i] = pucks[i].counts2rad(pucks[i].getProperty(Puck::MECH));
			}

			v_type errorAngle = (j2mp*home + zeroAngle) - currentAngle;
			for (size_t i = 0; i < DOF; ++i) {
				while (errorAngle[i] > M_PI) {
					errorAngle[i] -= 2*M_PI;
				}
				while (errorAngle[i] < -M_PI) {
					errorAngle[i] += 2*M_PI;
				}
			}

			// Check for exclusions
			for (size_t i = 0; i < DOF; ++i) {
				// If VERS < 118, then nothing useful is exposed on MECH; don't compensate
				if (pucks[i].getVers() < 118) {
					syslog(LOG_ERR, "  No zero-compensation for Puck %d: old firmware", pucks[i].getId());
					errorAngle[i] = 0;
					continue;
				}

				// If not ROLE & 256, then it's not an absolute encoder; don't compensate
				if ( !(pucks[i].getRole() & 256) ) {
					syslog(LOG_ERR, "  No zero-compensation for Puck %d: no absolute encoder", pucks[i].getId());
					errorAngle[i] = 0;
					continue;
				}

				// If the calibration data is out of range, don't compensate
				if (zeroAngle[i] > 2*M_PI  ||  zeroAngle[i] < 0) {
					syslog(LOG_ERR, "  No zero-compensation for Puck %d: bad calibration data", pucks[i].getId());
					errorAngle[i] = 0;
					continue;
				}
			}

			definePosition(home - m2jp*errorAngle);
			syslog(LOG_ERR, "  WAM zeroed with zero-compensation");
		} else {
			definePosition(home);
			syslog(LOG_ERR, "  WAM zeroed without zero-compensation");
		}
	}

	~LLW() {
		detail::purge(torqueGroups);
	}

	void definePosition(const jp_type& jp) {
		// Tell the safety logic to ignore the next several faults
		// (the position will appear to be changing rapidly)
		if (safetyPuck != NULL) {
			safetyPuck->setProperty(Puck::IFAULT, 8);  // TODO(dc): Why 8?
		}

		v_type pp = j2pp * jp;  // Convert from joint positions to Puck positions
		for (size_t i = 0; i < DOF; ++i) {
			pucks[i].setProperty(Puck::P, floor(pp[i]));
			usleep(1000);  // TODO(dc): necessary?
		}

		// Record the fact that the WAM has been zeroed
		if (safetyPuck != NULL) {
			safetyPuck->setProperty(Puck::ZERO, 1);
		}
	}

	void update() {

	}

	void setTorques(const jt_type& jt) {
		// Get around C++ address-of-static-member weirdness...
		static const size_t PUCKS_PER_TORQUE_GROUP = WamPuck::PUCKS_PER_TORQUE_GROUP;

		pt = j2pt * jt;  // Convert from joint torques to Puck torques

		size_t i = 0;
		for (size_t g = 0; g < torqueGroups.size(); ++g) {
			WamPuck::sendPackedTorques(bus, torqueGroups[g]->getId(), torquePropId, pt.data()+i, std::min(PUCKS_PER_TORQUE_GROUP, DOF-i));
			i += PUCKS_PER_TORQUE_GROUP;
		}
	}

protected:
	const CommunicationsBus& bus;
	std::vector<WamPuck> pucks;
	Puck* safetyPuck;
	PuckGroup wamGroup;
	std::vector<PuckGroup*> torqueGroups;

	jp_type home;
	sqm_type j2mp, m2jp, j2mt;
	sqm_type j2pp, j2pt;

//	jp_type jp, jp_1;
//	jv_type jv;

	v_type pt;
	int torquePropId;

private:
	DISALLOW_COPY_AND_ASSIGN(LLW);
};


}


#endif /* LLW_H_ */
