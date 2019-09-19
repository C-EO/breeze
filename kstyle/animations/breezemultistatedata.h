#ifndef breezemultistatedata_h
#define breezemultistatedata_h

/*************************************************************************
 * Copyright (C) 2014 by Hugo Pereira Da Costa <hugo.pereira@free.fr>    *
 *                                                                       *
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 * This program is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 * GNU General Public License for more details.                          *
 *                                                                       *
 * You should have received a copy of the GNU General Public License     *
 * along with this program; if not, write to the                         *
 * Free Software Foundation, Inc.,                                       *
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA .        *
 *************************************************************************/

#include "breezegenericdata.h"

namespace Breeze
{

//// //// //// //// /// /// /// // // /  /   /    /

class PropertyWrapperBase {
public:
    PropertyWrapperBase(QObject *object, QByteArray name): _object(object), _name(std::move(name)) {}
    PropertyWrapperBase(const PropertyWrapperBase &other) { *this = other; }
    PropertyWrapperBase & operator =(const PropertyWrapperBase &other) = default;
    virtual ~PropertyWrapperBase() = default;

    const QByteArray & name() const { return _name; }
    QObject * object() const { return _object; }

    operator QVariant() const { return _object->property(_name); }
    virtual PropertyWrapperBase & operator =(const QVariant &value)
    {
        const QVariant oldValue = _object->property(_name);
        if (oldValue.isValid() && oldValue.type() != value.type()) {
            qDebug("property \"%s\": new value type does not match previous value type (new: %s, old: %s) - trying to cast to original type.",
                   qPrintable(_name), value.typeName(), _object->property(_name).typeName());

            QVariant converted = value;
            bool ok = converted.convert(oldValue.type());
            Q_ASSERT(ok);
            _object->setProperty(_name, converted);
        } else {
            _object->setProperty(_name, value);
        }
        return *this;
    }

protected:
    QObject *_object;
    QByteArray _name;
};

template<typename T>
class PropertyWrapper: public PropertyWrapperBase {
public:
    PropertyWrapper(QObject *object_, const QByteArray &name_): PropertyWrapperBase(object_, name_)
    {
        QVariant value = object()->property(name());
        if (!value.isValid()) {
            object()->setProperty(name(), T());
        }
    }
    explicit PropertyWrapper(const PropertyWrapper<T> &other) : PropertyWrapperBase(other._object, other._name) {}
    PropertyWrapper<T> & operator =(const PropertyWrapper<T> &) = delete;
    PropertyWrapperBase & operator =(const PropertyWrapperBase &other) = delete;

    operator T() const { return _object->property(_name).template value<T>(); }
    PropertyWrapper<T> & operator =(const T &value) { _object->setProperty(_name, value); return *this; }
    PropertyWrapper<T> & operator =(const QVariant &value) override { *this = value.value<T>(); return *this; }
};

struct AbstractVariantInterpolator {
    virtual ~AbstractVariantInterpolator() = default;
    virtual QVariant interpolated(const QVariant &from, const QVariant &to, qreal progress) const = 0;
};

// QPropertyAnimation with support for passing custom interpolators as parameter and single
// PropertyWrapper instead of object + property parameters.
class CustomPropertyAnimation: public QPropertyAnimation {
public:
    CustomPropertyAnimation(const PropertyWrapperBase &property, QObject *parent = nullptr):
        QPropertyAnimation(property.object(), property.name(), parent), _interpolator(nullptr) {}

    CustomPropertyAnimation(const PropertyWrapperBase &property,
                            AbstractVariantInterpolator *interpolator,
                            QObject *parent = nullptr)
        : QPropertyAnimation(property.object(), property.name(), parent)
        , _interpolator(interpolator)
    {}

    ~CustomPropertyAnimation() override { delete _interpolator; }

protected:
    QVariant interpolated(const QVariant &from, const QVariant &to, qreal progress) const override {
        if (_interpolator != nullptr) {
            return _interpolator->interpolated(from, to, progress);
        }
        return QPropertyAnimation::interpolated(from, to, progress);
    }

private:
    AbstractVariantInterpolator *_interpolator;
};

//// //// //// //// /// /// /// // // /  /   /    /

#if 0
    class ValueAnimator: public QAbstractAnimation
    {
    public:
        ValueAnimator(QVariant *value, const QVariant &from, const QVariant &to = QVariant(),
                      qreal duration = qQNaN(), QEasingCurve curve = QEasingCurve::Linear);

        qreal normalizedDuration() const;

        int duration() const override
        {
        }

    protected:
        void updateCurrentTime(int currentTime) override
        {
        }

    private:
        QVariant *_value;
        QVariant _from;
        QVariant _to;
        qreal _duration;
        QEasingCurve _curve;
    };

#endif
    class AnimationTimeline: public QAbstractAnimation
    {
        Q_OBJECT

    public:
        struct Entry {
            Entry(qreal normalizedStartTime, const PropertyWrapperBase &property, QVariant from, QVariant to = QVariant(), qreal normalizedDuration = 0.0, QEasingCurve easingCurve = QEasingCurve::Linear)
                : normalizedStartTime(normalizedStartTime)
                , property(property)
                , from(std::move(from))
                , to(std::move(to))
                , normalizedDuration(normalizedDuration)
                , easingCurve(std::move(easingCurve))
            {
                // Just to not introduce separate constructor for simple setter entry
                if(!this->to.isValid()) {
                    std::swap(this->from, this->to);
                }
            }

            qreal normalizedStartTime;
            PropertyWrapperBase property;
            QVariant from;
            QVariant to;
            qreal normalizedDuration;
            QEasingCurve easingCurve;
        };
        using EntryList = QList<Entry>;

        AnimationTimeline(QObject *parent, int duration = 0, const EntryList &entries = {})
            : QAbstractAnimation(parent)
            , _entries(entries)
            , _firstNotStartedIndex(0)
            , _durationMs(duration)
        {
            for(int i = 0; i < _entries.count(); ++i) {
                auto animation = animationFromEntry(_entries[i]);
                updateAnimationDuration(animation, _entries[i]);
                if (animation != nullptr) {
                    connect(animation, &QPropertyAnimation::valueChanged, this, &AnimationTimeline::valueChanged);
                }
                _animations.append(animation);
            }
        }

        void setDuration(int durationMs)
        {
            _durationMs = durationMs;
            for(int i = 0; i < _entries.count(); ++i) {
                updateAnimationDuration(_animations[i], _entries[i]);
            }
        }
        int duration() const override { return _durationMs; }

        // XXX: remove
        bool isfirst() {
            static void *_loggedobj = parent();
            return _loggedobj == parent();
        }

Q_SIGNALS:
    void valueChanged();

#define dbg(...) if(isfirst()) { qDebug(__VA_ARGS__); } else {}
    protected:
        void updateCurrentTime(int currentTime) override {
            bool changed = false;
            for(int i = _firstNotStartedIndex; i < _entries.length(); ++i) {
                auto entry = _entries[i];
                auto animation = _animations[i];

                int startTime = _durationMs * entry.normalizedStartTime;
                int duration = _durationMs * entry.normalizedDuration;
                int endTime = startTime + duration;

                int startDelay = currentTime - startTime;
                int remaining = endTime - currentTime;

                if (startDelay < 0) {
                    // Too early for this and all following entries
                    break;
                }

                if (remaining > 0) {
                    animation->setCurrentTime(startDelay);
                    animation->start();
                    dbg("t=%4d; i=%2d; start=%4d; duration=%4d; end=%4d; delay=% 4d; remaining=% 4d  -  start",
                        currentTime, i, startTime, duration, endTime, startDelay, remaining);
                } else {
                    // missed animation or durationless entry, just apply final state
                    entry.property = entry.to;
                    changed = true;
                    dbg("t=%4d; i=%2d; start=%4d; duration=%4d; end=%4d; delay=% 4d; remaining=% 4d  -  set final",
                        currentTime, i, startTime, duration, endTime, startDelay, remaining);
                }
                _firstNotStartedIndex = i + 1;
            }
            if (changed) {
                emit valueChanged();
            }
        }

        void updateState(QAbstractAnimation::State newState, QAbstractAnimation::State oldState) override
        {
            Q_UNUSED(oldState);
            dbg("updateState: %d", newState);

            switch(newState) {
            case Running:
                for (int i = 0; i < _entries.length(); ++i) {
                    if (!_entries[i].from.isValid() && !qFuzzyIsNull(_entries[i].normalizedDuration)) {
                        dbg("           set start value for %s", qPrintable(_entries[i].property.name()));
                        _animations[i]->setStartValue(_entries[i].property);
                    }
                }
                _firstNotStartedIndex = 0;
                break;
            case Stopped:
                for (int i = 0; i < _entries.length(); ++i) {
                    if (_animations[i] != nullptr) {
                        _animations[i]->stop();
                    }
                }
                break;
            default: break;
            }
        }

    private:
        inline QPropertyAnimation *animationFromEntry(const Entry &entry) {
            if(qFuzzyIsNull(entry.normalizedDuration)) {
                // this is just a setter TODO: handle in updateCurrentTime
                return nullptr;
            }
            auto animation = new QPropertyAnimation(parent(), entry.property.name(), this);
            animation->setStartValue(entry.from);
            animation->setEndValue(entry.to);
            animation->setEasingCurve(entry.easingCurve);
            return animation;
        }

        void updateAnimationDuration(QPropertyAnimation *animation, const Entry &entry) {
            if(animation == nullptr) {
                return;
            }
            animation->setDuration(_durationMs * entry.normalizedDuration);
        }
    public: // TODO: remove
        EntryList _entries;
        QList<QPropertyAnimation *> _animations;
        int _firstNotStartedIndex;

        int _durationMs;
    };


    //* Tracks arbitrary states (e.g. tri-state checkbox check state)
    class MultiStateData: public GenericData
    {

        Q_OBJECT

        public:

        //* constructor
        MultiStateData( QObject* parent, QWidget* target, int duration, QVariant state = QVariant() ):
            GenericData( parent, target, duration ),
            _initialized( false ),
            _state( state ),
            _previousState( state )
        {}

        //* destructor
        virtual ~MultiStateData()
        {}

        /**
        returns true if state has changed
        and starts timer accordingly
        */
        virtual bool updateState( const QVariant &value );

        virtual QVariant state() const { return _state; }
        virtual QVariant previousState() const { return _previousState; }

        QMap<unsigned, QAbstractAnimation *> anims;

        private:

        bool _initialized;
        QVariant _state;
        QVariant _previousState;

    };

}

#endif